#ifndef TIMER_H
#define TIMER_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <time.h>

#include "../log/log.h"
#include "../http_conn/http_conn.h"

class util_timer;

struct client_data{
    sockaddr_in address;
    int sockfd;
    util_timer *timer;
};

class util_timer{
public:
    util_timer() : prev(nullptr), next(nullptr){}

    time_t expire;
    void (*cb_func)(client_data *);
    client_data *user_data;
    util_timer *prev;
    util_timer *next;
};

class sort_util_timer{
private:
    util_timer *head;
    util_timer *tail;

    void add_timer(util_timer *timer, util_timer *lst_head);
public:
    sort_util_timer() : head(nullptr), tail(nullptr) {}
    ~sort_util_timer(){
        util_timer *tmp = head;
        while(tmp){
            head = head->next;
            delete tmp;
            tmp = head;
        }
    }

    void add_timer(util_timer *timer);
    void adjust_timer(util_timer *timer);
    void del_timer(util_timer *timer);
    void tick();
};

class Utils{
public:
    Utils() {};
    ~Utils() {};

    void init(int timeslot);

    int setnonblocking(int fd);

    void addfd(int epollfd, int fd, bool one_shot, int trigmode);

    static void sig_handler(int sig);

    void addsig(int sig, void(handler)(int), bool restart=true);

    void timer_handler();

    void show_error(int connfd, const char *info);
    
public:
    static int *u_pipefd;
    sort_util_timer m_timer_lst;
    static int u_epollfd;
    int m_timeslot;
};

void cb_func(client_data *user_data);

#endif