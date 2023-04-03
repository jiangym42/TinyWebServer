#include "http_conn.h"
#include<mysql/mysql.h>
#include<iostream>
#include<map>

int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;

const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

map<string, string> users;
locker m_lock;

void http_conn::initmysql_result(Connection_pool *connPool){
    connP = connPool;
    MYSQL *mysql = nullptr;
    
    ConnectionRAII mysqlconn(&mysql, connPool);

    if(mysql_query(mysql, "SELECT username, passwd FROM user")){
        LOG_ERROR("SELECT error: %s\n", mysql_error(mysql));
    }

    MYSQL_RES *result = mysql_store_result(mysql);

    int num_fields = mysql_num_fields(result);

    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    while(MYSQL_ROW row = mysql_fetch_row(result)){
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

void setnonblocking(int fd){
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
}

void addfd(int epollfd, int fd, bool one_shot, int trigmode){
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP; //EPOLLRDHUP：对端断开连接 EPOLLIN：有读事件
    if(trigmode == 1){
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    }
    else{
        event.events = EPOLLIN | EPOLLRDHUP;
    }
    if(one_shot){
        event.events |= EPOLLONESHOT; //避免两个线程同时操作一个文件描述符
    }

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void removefd(int epollfd, int fd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

void modfd(int epollfd, int fd, int ev, int trigmode){
    //重置socket上的EPOLLONESHOT事件
    epoll_event event;
    event.data.fd = fd;
    if(trigmode == 1){
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP | EPOLLET;
    }
    else{
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    }
    
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

void http_conn::init(int sockfd, const sockaddr_in & addr, char *root, int trigmode, int close_log, string user, string passwd, string sqlname){
    m_sockfd = sockfd;
    m_address = addr;
    m_trigmode = trigmode;
    m_close_log = close_log;
    doc_root = root;

    //设置端口复用
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    addfd(m_epollfd, sockfd, true, m_trigmode);
    m_user_count++;

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();
}

void http_conn::init(){
    mysql = nullptr;
    connP = nullptr;
    cgi = 0;

    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;

    m_checked_index = 0;
    m_start_line = 0;
    m_read_idx = 0;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_host = 0;
    m_content_length = 0;
    m_write_index = 0;

    bytes_to_send = 0;
    bytes_have_send = 0;

    improv = 0;
    timer_flag = 0;
    m_state = 0;

    bzero(m_read_buf, READ_BUFFER_SIZE);
    bzero(m_write_buf, WRITE_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
}

void http_conn::close_conn(){
    if(m_sockfd != -1){
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

void http_conn::unmap(){
    if(m_file_addr){
        munmap(m_file_addr, m_file_stat.st_size);
        m_file_addr = 0;
    }
}

bool http_conn::read(){
    if(m_read_idx >= READ_BUFFER_SIZE)
        return false;
    int bytes_read = 0;

    if(m_trigmode == 0){
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE-m_read_idx, 0);
        m_read_idx += bytes_read;

        if(bytes_read <= 0){
            return false;
        }
        return true;
    }
    else{
        while(true){
            bytes_read = recv(m_sockfd, m_read_buf+m_read_idx, READ_BUFFER_SIZE-m_read_idx, 0);
            if(bytes_read == -1){
                if(errno == EAGAIN || errno == EWOULDBLOCK){
                    break;
                }
                return false;
            }

            else if(bytes_read == 0){
                return false;
            }

            m_read_idx += bytes_read;
        }
    }
    return true;
}

http_conn::HTTP_CODE http_conn::process_read(){
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    while((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || (line_status = parse_line()) == LINE_OK){
        //GET读到一行，GET请求报文中，每一行都要以\r\n结束，对报文拆解时，使用从状态机的状态即可
        //POST请求中，消息体末尾没有任何字符，所以不能使用从状态机的状态，转而使用主状态机的状态作为入口条件
        //解析完消息体后，报文的完整解析就完成了，但是此时主状态机的状态仍然是CHECK_STATE_CONTENT，此时就需要将line_status状态修改为LINE_OPEN就可以跳出循环
        text = get_line();
        m_start_line = m_checked_index;
        printf("process 1-line: %s\n", text);

        switch(m_check_state){
            case CHECK_STATE_REQUESTLINE:
            {
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:
            {
                ret = parse_headers(text);
                if(ret == BAD_REQUEST)
                    return BAD_REQUEST;
                else if(ret == GET_REQUEST){
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                ret = parse_content(text);
                if(ret == GET_REQUEST)
                    return do_request();
                line_status = LINE_OPEN;
                break;
            }
            default:
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

http_conn::LINE_STATUS http_conn::parse_line(){
    char temp;
    for(; m_checked_index < m_read_idx; ++m_checked_index){
        temp = m_read_buf[m_checked_index];
        if(temp == '\r'){
            if((m_checked_index + 1) == m_read_idx){
                return LINE_OPEN;
            }
            else if(m_read_buf[m_checked_index+1] == '\n'){
                m_read_buf[m_checked_index++] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }

        else if(temp == '\n'){
            if((m_checked_index > 1) && (m_read_buf[m_checked_index-1] == '\r')){
                m_read_buf[m_checked_index-1] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

http_conn::HTTP_CODE http_conn::parse_request_line(char *text){
    // GET http://192.168.137.128:9999/index.html HTTP/1.1
    m_url = strpbrk(text, " \t");
    if(!m_url){
        return BAD_REQUEST;
    }

    *m_url++ = '\0';
    char *method = text;
    if(strcasecmp(method, "GET") == 0){
        m_method = GET;
    }
    else if(strcasecmp(method, "POST") == 0){
        m_method = POST;
        cgi = 1;
    }
    else{
        return BAD_REQUEST;
    }

    // m_url += strspn(m_url," \t");

    m_version = strpbrk(m_url, " \t");
    if(!m_version){
        return BAD_REQUEST;
    }

    *m_version++ = '\0';
    // m_version += strcspn(m_version, " \t");

    if(strcasecmp(m_version, "HTTP/1.1") != 0){
        return BAD_REQUEST;
    }

    if(strncasecmp(m_url, "HTTP://", 7) == 0){
        m_url += 7;
        m_url = strchr(m_url,  '/');
    }

    if(strncasecmp(m_url, "HTTPS://", 8) == 0){
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if(!m_url || m_url[0] != '/'){
        return BAD_REQUEST; 
    }

    if(strlen(m_url) == 1){
        strcat(m_url, "judge.html");
    }

    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
} 

http_conn::HTTP_CODE http_conn::parse_headers(char *text){
    if(text[0] == '\0'){
        if(m_content_length != 0){
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }

    else if(strncasecmp(text, "Connection:", 11) == 0){
        text += 11;
        text += strspn(text, " \t");
        if(strcasecmp(text, "Keep-alive") == 0){
            m_linger = true;
        }
    }

    else if(strncasecmp(text, "Content-Length:", 15) == 0){
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }

    else if(strncasecmp(text, "Host:", 5) == 0){
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else{
        printf("unkonwn header! %s\n", text);
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_content(char *text){
    if(m_read_idx >= m_content_length + m_checked_index){
        text[m_content_length] = '\0';

        m_string = text;

        return GET_REQUEST;
    }
    return NO_REQUEST;
}

bool http_conn::add_response(const char * format, ...){
    if(m_write_index >= WRITE_BUFFER_SIZE){
        return false;
    }

    va_list arg_list;
    va_start(arg_list, format);

    int len = vsnprintf(m_write_buf+m_write_index, WRITE_BUFFER_SIZE - 1 - m_write_index, format, arg_list);
    if(len >= WRITE_BUFFER_SIZE - 1 - m_write_index){
        va_end(arg_list);
        return false;
    }

    m_write_index += len;
    va_end(arg_list);
    return true;
}

http_conn::HTTP_CODE http_conn::do_request(){
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    const char *p = strrchr(m_url, '/');

    if(cgi == 1 && (*(p+1) == '2' || *(p+1) == '3')){
        char flag = m_url[1];

        char *m_url_real = (char *)malloc(sizeof(char) * 20);

        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url+2);
        // printf("\n===================m_url_real:%s=====================\n", m_url_real);
        strncpy(m_real_file+len, m_url_real, FILENAME_LEN-len-1);
        // printf("\n===================m_real_file1:%s=====================\n", m_real_file);


        //user=123&passwd=123
        char name[100];
        char password[100];
        int i = 0;
        for(i = 5; m_string[i] != '&'; ++i){
            name[i-5] = m_string[i];
        }
        name[i-5] = '\0';

        int j = 0;
        for(i = i+10; m_string[i] != '\0'; ++i, ++j){
            password[j] = m_string[i];
        }
        password[j] = '\0';

        if(*(p+1) == '2'){
            //登录
            if(users.find(name) != users.end() && users[name] == password)
                strcpy(m_url, "/welcome.html");
            else
                strcpy(m_url, "/logError.html");
        }

        if(*(p+1) == '3'){
            //注册
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            sprintf(sql_insert, "insert into user(username, passwd) values('%s', '%s')", name, password);

            if(users.find(name) == users.end()){
                //注册的name是有效的
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();

                if(res == 0){
                    strcpy(m_url, "/log.html");
                }
                else
                    strcpy(m_url, "/registerError.html");
            }

            else
                strcpy(m_url, "/registerError.html");

            free(sql_insert);
        }
    
    }
    if(*(p+1) == '0'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file+len, m_url_real, strlen(m_url_real));
        // printf("\n===================m_real_file2:%s=====================\n", m_real_file);
        free(m_url_real);
    }

    else if(*(p+1) == '1'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file+len, m_url_real, strlen(m_url_real));
        // printf("\n===================m_real_file3:%s=====================\n", m_real_file);
        free(m_url_real);
    }
    else if(*(p+1) == '5'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file+len, m_url_real, strlen(m_url_real));
        // printf("\n===================m_real_file4:%s=====================\n", m_real_file);
        free(m_url_real);
    }
    else
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
        // printf("\n===================m_real_file5:%s=====================\n", m_real_file);

    if(stat(m_real_file, &m_file_stat) < 0){
        //stat函数用于取得指定文件的文件属性，并将文件属性存储在结构体stat里
        return NO_RESOURCE;
    }

    if(!(m_file_stat.st_mode & S_IROTH)){
        return FORBIDDEN_REQUEST;
    }

    if(S_ISDIR(m_file_stat.st_mode)){
        return BAD_REQUEST;
    }

    int fd = open(m_real_file, O_RDONLY);
    m_file_addr = (char *) mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

bool http_conn::add_status_line(int status, const char * title){
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len){
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
    return true;

}

bool http_conn::add_content_length(int content_len){
    return add_response("Content-Length: %d\r\n", content_len);
}

bool http_conn::add_linger(){
    return add_response("Connection: %s\r\n", m_linger == true ? "Keep-alive" : "close");
}

bool http_conn::add_blank_line(){
    return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char *content){
    return add_response("%s", content);
}

bool http_conn::add_content_type(){
    return add_response("Content-Type: %s\r\n", "text/html");
}

bool http_conn::process_write(HTTP_CODE ret){
    switch(ret){
        case INTERNAL_ERROR:
        {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form)){
                return false;
            }
            break;
        }
        case BAD_REQUEST:
        {
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if(!add_content(error_400_form)){
                return false;
            }
            break;
        }
        case NO_RESOURCE:
        {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if(!add_content(error_404_form))
                return false;
            break;
        }
        case FORBIDDEN_REQUEST:
        {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_403_form)){
                return false;
            }
            break;
        }
        case FILE_REQUEST:
        {
            add_status_line(200, ok_200_title);
            if(m_file_stat.st_size != 0){
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_index;
                m_iv[1].iov_base = m_file_addr;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;

                bytes_to_send = m_write_index + m_file_stat.st_size;
                return true;
            }
            else{
                const char *ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if(!add_content(ok_string))
                    return false;
            }
        }
        default:
            return false;
    } 
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_index;
    m_iv_count = 1;
    bytes_to_send = m_write_index;
    return true;
}

bool http_conn::write(){    
    int temp = 0;
    int newadd = 0;

    if(bytes_to_send == 0){
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_trigmode);
        init();
        return true;
    }

    while(true){
        temp = writev(m_sockfd, m_iv, m_iv_count);
        //由于报文消息报头较小，第一次传输后，需要更新m_iv[1].iov_base和iov_len
        //m_iv[0].iov_len置成0，不用传输响应消息头
        //每次传输后都要更新下次传输文件的起始位置和长度
        if(temp > 0){
            bytes_have_send += temp;
            newadd = bytes_have_send - m_write_index;
        }
        if(temp <= -1){
            if(errno == EAGAIN){
                if(bytes_have_send >= m_iv[0].iov_len){
                    m_iv[0].iov_len = 0;
                    m_iv[1].iov_base = m_file_addr + newadd;
                    m_iv[1].iov_len = bytes_to_send;
                }
                else{
                    m_iv[0].iov_base = m_write_buf + bytes_to_send;
                    m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
                }

                modfd(m_epollfd, m_sockfd, EPOLLOUT,m_trigmode);
                return true;
            }
            unmap();
            return false;
        }

        bytes_to_send -= temp;

        if(bytes_to_send <= 0){
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_trigmode);

            if(m_linger){
                init();
                return true;
            }
            else{
                return false;
            }
        }
    }
}

void http_conn::process(){
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST){
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_trigmode);
        return;
    }

    bool write_ret = process_write(read_ret);
    if(!write_ret){
        close_conn();
    }

    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_trigmode);
}
