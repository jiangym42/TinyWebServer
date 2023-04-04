#ifndef WEBSERVER_H
#define WEBSERVER_H

#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<stdio.h>
#include<unistd.h>
#include<errno.h>
#include<fcntl.h>
#include<stdlib.h>
#include<cassert>
#include<sys/epoll.h>

#include "../threadpool/threadpool.h"
#include "../http_conn/http_conn.h"

const int MAX_FD = 65535;
const int MAX_EVENT_NUMBER = 10000;
const int TIMESLOT = 5;

class webserver{
public:
    webserver();
    ~webserver();

    void init(int port, string user, string passwd, string dbname, int logwrite, int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model, bool heaptimer);
    void thread_pool();
    void sql_pool();
    void log_write();
    void trig_mode();
    void eventListen();
    void eventLoop();
    void timer(int connfd, struct sockaddr_in client_address);
    
    void adjust_timer(util_timer *timer);
    void adjust_timer(TimerNode *timer);

    void deal_timer(TimerNode *timer, int sockfd);
    void deal_timer(util_timer *timer, int sockfd);
    
    bool dealclientdata();
    bool dealwithsignal(bool& timeout, bool& stop_server);
    void dealwithread(int sockfd);
    void dealwithwrite(int sockfd);

    int m_port;
    char *m_root;
    int m_log_write;
    int m_close_log;
    int m_actormodel;
    int m_pipefd[2];
    int m_epollfd;

    http_conn *users;

    Connection_pool *m_connPool;
    string m_user;
    string m_passwd;
    string m_dbname;
    int m_sql_num;

    threadpool<http_conn> *m_pool;
    int m_thread_num;

    epoll_event events[MAX_EVENT_NUMBER];

    int m_listenfd;
    int m_opt_linger;
    int m_trigmode;
    int m_listentrigmode;
    int m_conntrigmode;

    client_data *users_timer;
    HeapTimer *m_heap_timer_ptr;

    bool m_heap_timer;

    Utils utils;
};

#endif