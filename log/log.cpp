#include "log.h"

#include<string.h>
#include<sys/time.h>
#include<stdarg.h>
#include<pthread.h>
#include<time.h>

using namespace std;

Log::Log(){
    m_count = 0;
    m_is_async = false;
}

Log::~Log(){
    if(m_fp != NULL){
        fclose(m_fp);
    }
}

bool Log::init(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size){
    //初始化生成日志文件，服务器启动按当前时刻创建日志，前缀为时间，后缀为自定义的log名
    if(max_queue_size >=1){
        //通过判断阻塞队列的大小来控制日志同步写还是异步写
        m_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size);
        //创建线程异步写日志
        pthread_t tid;
        //创建一个线程从阻塞队列中取数据
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    m_close_log = close_log;
    //初始化日志缓冲区大小
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);
    //初始化日志最大行数
    m_split_lines = split_lines;

    //获取时间
    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    //在file_name中找到第一次出现“/”的位置并返回指向该位置的指针
    const char *p = strrchr(file_name, '/');
    char log_full_name[256] = {0};

    if(p == NULL){
        //p==null表示输入的文件名中没有“/”的出现，直接将时间+文件名作为日志名
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year+1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }

    else{
        //先拷贝“/”后的名字到log_name中
        strcpy(log_name, p+1);
        //将file_name指向的字符串复制到dir_name中，最多复制p-file_name+1个？
        strncpy(dir_name, file_name, p - file_name+1);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    //获取当前名字
    m_today = my_tm.tm_mday;
    //写入方式打开文件，初始化这个文件指针
    m_fp = fopen(log_full_name, "a");
    if(m_fp == NULL){
        return false;
    }
    return true;
}

void Log::write_log(int level, const char *format, ...){
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;

    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    char s[16] = {0};
    switch(level){
        case 0:
        {
            strcpy(s, "[debug]:");
            break;
        }
        case 1:
        {
            strcpy(s, "[info]:");
            break;
        }
        case 2:
        {
            strcpy(s, "[warn]:");
            break;
        }
        case 3:
        {
            strcpy(s, "[erro]:");
            break;
        }
        default:
        {
            strcpy(s, "[info]:");
            break;
        }
    }

    m_mutex.lock();
    m_count++;

    //日志不是今天 or 达到最大行数
    if(m_today != my_tm.tm_mday || m_count % m_split_lines == 0){
        char new_log[256] = {0};
        fflush(m_fp);
        fclose(m_fp);

        char tail[16] = {0};
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

        if(m_today != my_tm.tm_mday){
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }
        else{
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }

        m_fp = fopen(new_log, "a");
    }

    m_mutex.unlock();

    va_list valst;
    va_start(valst, format);

    string log_str;
    m_mutex.lock();

    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                    my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                    my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, 
                    s);
    int m = vsnprintf(m_buf + n, m_log_buf_size-1, format, valst);
    m_buf[n+m] = '\n';
    m_buf[n+m+1] = '\0';

    log_str = m_buf;
    m_mutex.unlock();

    if(m_is_async && !m_log_queue->full()){
        m_log_queue->push(log_str);
    }

    else{
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }

    va_end(valst);
}

void Log::flush(){
    m_mutex.lock();
    fflush(m_fp);
    m_mutex.unlock();
}
