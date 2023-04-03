#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include<sys/epoll.h>
#include<cstdio>
#include<cstdlib>
#include<unistd.h>
#include<signal.h>
#include<sys/types.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<sys/stat.h>
#include<sys/mman.h>
#include<stdarg.h>
#include<errno.h>
#include<sys/uio.h>
#include<string.h>
#include<mysql/mysql.h>
#include<regex>
#include<string>

#include "../locker/locker.h"
#include "../sql_conn/sql_connection_pool.h"
#include "../timer/timer.h"
#include "../log/log.h"


class http_conn{
public:
    static int m_epollfd;
    static int m_user_count;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    static const int FILENAME_LEN = 200;

    int improv;
    int timer_flag;
    int m_state;

    MYSQL *mysql;
    Connection_pool *connP;

    enum METHOD{GET = 0, POST, HEAD, PUT, DELETE, OPTIONS, CONNECT};
    enum CHECK_STATE{CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT};
    enum LINE_STATUS{LINE_OK = 0, LINE_BAD, LINE_OPEN};
    enum HTTP_CODE{NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION};

    http_conn(){};
    ~http_conn(){};

    void process(); //处理客户端的请求

    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);
    void close_conn();
    bool read();
    bool write();

    void initmysql_result(Connection_pool *connPool);

    sockaddr_in *get_address(){ return &m_address; }

private:
    int m_sockfd;
    sockaddr_in m_address;

    map<string, string> m_users;
    int m_trigmode;
    int m_close_log;
    char *doc_root;

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];

    char *m_string;
    int cgi;

    //============
    char m_read_buf[READ_BUFFER_SIZE];
    int m_read_idx;
    int m_checked_index;
    int m_start_line;
    
    //=============
    char *m_url;
    char *m_version;
    char *m_host;
    bool m_linger;
    int m_content_length;


    //=============
    CHECK_STATE m_check_state;
    METHOD m_method;

    //============
    char m_real_file[FILENAME_LEN];
    char m_write_buf[WRITE_BUFFER_SIZE];
    char *m_file_addr;
    struct stat m_file_stat;
    struct iovec m_iv[2];
    int m_iv_count;
    int m_write_index;
    int bytes_to_send;
    int bytes_have_send;

    //=============
    HTTP_CODE process_read();
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    LINE_STATUS parse_line();
    HTTP_CODE do_request();

    //=============
    bool process_write(HTTP_CODE ret);
    bool add_response(const char * format, ...);
    bool add_content(const char * content);
    bool add_content_type();
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_linger();
    bool add_blank_line();
    bool add_content_length(int content_length);

    char * get_line(){ return m_read_buf + m_start_line; }

    void unmap();
    void init();
};



#endif