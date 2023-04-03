#ifndef LOG_H
#define LOG_H

#include<stdio.h>
#include<iostream>
#include<string>
#include<stdarg.h>
#include<pthread.h>

#include "block_queue.h"

using namespace std;

class Log{
private:
    char dir_name[128];//log文件名
    char log_name[128];//log路径名

    int m_split_lines;//最大行数
    int m_log_buf_size;//log缓冲区大小
    long long m_count;//log行数记录
    int m_today;//记录今天是哪一天

    FILE *m_fp;//log文件指针
    char *m_buf;//log缓冲区指针

    block_queue<string> *m_log_queue;//阻塞队列

    bool m_is_async;//是否同步标志位
    locker m_mutex;

    int m_close_log;

public:
    static Log *getInstance(){
        //懒汉模式获取实例
        static Log instance;
        return &instance;
    }

    static void *flush_log_thread(void *args){
        //线程通过调用私有的异步写日志函数完成从阻塞队列中取出一条数据并将其写入文件指针指向的文件中
        Log::getInstance()->async_write_log();
    }

    bool init(const char *file_name, int close_log, int log_buf_size=8192, int split_lines = 5000000, int max_queue_size = 0);

    void write_log(int level, const char *format, ...);

    void flush();

private:
    Log();
    virtual ~Log();

    void *async_write_log(){
        string single_log;

        while(m_log_queue->pop(single_log)){
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp);
            m_mutex.unlock();
        }
    }
};

#define LOG_DEBUG(format, ...) Log::getIntance()->write_log(0, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) Log::getInstance()->write_log(1, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) Log::getInstance()->write_log(2, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) Log::getInstance()->write_log(3, format, ##__VA_ARGS__)

#endif
