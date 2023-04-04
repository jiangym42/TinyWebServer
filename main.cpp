// #include<cstdio>
// #include<cstdlib>
// #include<string.h>
// #include<sys/socket.h>
// #include<netinet/in.h>
// #include<arpa/inet.h>
// #include<unistd.h>
// #include<error.h>
// #include<fcntl.h>
// #include<sys/epoll.h>
// #include<libgen.h>
// #include<signal.h>
// #include<cassert>
// #include "locker.h"
// #include "threadpool.h"
// #include "http_conn.h"
// #include "timer.h"
// #include "log.h"
// #include "sql_connection_pool.h"

// #define listenfdLT
// #define MAX_FD 65535
// #define MAX_EVENT_NUMBER 10000
// #define TIMESLOT 5

// void addsig(int sig, void(* handler)(int), bool restart = true){
//     struct sigaction sa;
//     memset(&sa, '\0', sizeof(sa));
//     sa.sa_handler = handler;
//     if(restart == true){
//         //当信号处理函数返回后，设置了SA_RESTART不会让系统调用返回失败，而是让被该信号中断的系统调用自动恢复
//         sa.sa_flags |= SA_RESTART;
//     }
//     sigfillset(&sa.sa_mask);//将参数set信号集初始化，然后将所有的信号加入此信号集中，即将所有的信号标志设置为1
//                             //sa_mask 成员用来指定在信号处理函数执行期间需要被屏蔽的信号
//     sigaction(sig, &sa, NULL);
// }

// static int pipefd[2];
// static sort_util_timer timer_lst;
// static int epollfd = 0;

// extern void addfd(int epollfd, int fd, bool one_shot);

// extern void removefd(int epollfd, int fd);

// extern void modfd(int epollfd, int fd, int ev);

// extern void setnonblocking(int fd);

// void sig_handler(int sig){
//     int save_errno = errno;
//     int msg = sig;
//     send(pipefd[1], (char*)&msg, 1, 0);//sighandler向pipefd中发送信号信息
//     errno = save_errno;
// }

// void timer_handler(){
//     timer_lst.tick();
//     alarm(TIMESLOT);
// }

// void cb_func(client_data *user_data){
//     epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
//     assert(user_data);
//     close(user_data->sockfd);
//     http_conn::m_user_count--;
// }

// int main(int argc, char *argv[]){
//     printf("hello webserver\n");
//     Log::getInstance()->init("serverlog", 2000, 80000, 8);

//     if(argc <= 1){
//         printf("command should be : %s port_number\n", basename(argv[0]));
//         return 1;
//     }

//     int port = atoi(argv[1]);
    
//     addsig(SIGPIPE, SIG_IGN);
//     threadpool<http_conn>* pool = NULL;

//     Connection_pool *connPool = Connection_pool::GetInstance();

//     connPool->init("localhost", "jym", "1999", "mydb", 3306, 8);


//     try{
//         pool = new threadpool<http_conn>(1, connPool);
//         printf("threadPool initialized...\n");
//     }catch(...){
//         return 1;
//     }

//     http_conn * users = new http_conn[MAX_FD];

//     users->initmysql_result(connPool);
    
//     int listenfd = socket(PF_INET, SOCK_STREAM, 0);
//     int ret = 0;
//     int reuse = 1;
//     setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

//     struct sockaddr_in address;
//     bzero(&address, sizeof(address));
//     address.sin_family = AF_INET;
//     address.sin_addr.s_addr = INADDR_ANY;
//     address.sin_port = htons(port);

//     ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
//     ret = listen(listenfd, 5);

//     epoll_event events[MAX_EVENT_NUMBER];
//     epollfd = epoll_create(5);
    
//     addfd(epollfd, listenfd, false);
//     http_conn::m_epollfd = epollfd;

//     //设置定时器
//     ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
//     assert(ret != -1);
//     setnonblocking(pipefd[1]);
//     addfd(epollfd, pipefd[0], false);

//     addsig(SIGALRM, sig_handler, false);
//     addsig(SIGTERM, sig_handler, false);
//     bool stop_server = false;

//     client_data *user_timer = new client_data[MAX_FD];
//     bool timeout = false;
//     alarm(TIMESLOT);

//     while(!stop_server){
//         int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
//         if(num < 0 && errno != EINTR){
//             LOG_ERROR("%s", "epoll failure");
//             break;
//         }

//         for(int i = 0; i < num; i++){
//             int sockfd = events[i].data.fd;

//             if(sockfd == listenfd){
// #ifdef listenfdLT
//                 //printf("listen recvd: %d\n", sockfd);
//                 struct sockaddr_in client_address;
//                 socklen_t client_addrlen = sizeof(client_address);
                
//                 int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlen);

//                 if(connfd < 0){
//                     LOG_ERROR("%S:errno is:%d", "accept error", errno);
//                     continue;
//                 }

//                 if(http_conn::m_user_count >= MAX_FD){
//                     close(connfd);
//                     LOG_ERROR("%S", "Interal server busy");
//                     continue;
//                 }

//                 users[connfd].init(connfd, client_address);
//                 //为新连接创建一个timer
//                 //设置user_timer基本信息
//                 user_timer[connfd].address = client_address;
//                 user_timer[connfd].sockfd = connfd;

//                 util_timer *timer = new util_timer;
//                 timer->user_data = &user_timer[connfd];
//                 timer->cb_func = cb_func;
//                 time_t cur = time(NULL);
//                 timer->expire = cur + 3 * TIMESLOT;
//                 user_timer[connfd].timer = timer;
//                 timer_lst.add_timer(timer);

// #endif
//             }
//             else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
//                 // users[sockfd].close_conn();
//                 util_timer *timer = user_timer[sockfd].timer;
//                 timer->cb_func(&user_timer[sockfd]);
//                 if(timer){
//                     timer_lst.del_timer(timer);
//                 }
//             }
//             else if((sockfd == pipefd[0]) && (events[i].events & EPOLLIN)){
//                 int sig;
//                 char signals[1024];
//                 ret = recv(pipefd[0], signals, sizeof(signals), 0);
//                 //ret接收到pipefd传来的信号
//                 if(ret == -1){
//                     continue;
//                 }

//                 else if(ret == 0){
//                     continue;
//                 }

//                 else{
//                     for(int i = 0; i < ret; ++i){
//                         switch(signals[i]){
//                             case SIGALRM:
//                             {
//                                 timeout = true;
//                                 break;
//                             }
//                             case SIGTERM:
//                             {
//                                 stop_server=true;
//                             }
//                         }
//                     }
//                 }

//             }
//             else if(events[i].events & EPOLLIN){

//                 util_timer *timer = user_timer[sockfd].timer;
//                 if(users[sockfd].read()){
//                     LOG_INFO("deal with a client");
//                     pool->append(users + sockfd);

//                     if(timer){
//                         //连接上有新的事件触发，更新该连接timer时间，并调整timer在lst上的位置
//                         time_t cur = time(NULL);
//                         timer->expire = cur + 3 * TIMESLOT;
//                         LOG_INFO("%s", "adjust timer once");
//                         Log::getInstance()->flush();
//                         timer_lst.adjust_timer(timer);
//                     }
//                 }
//                 else{
//                     //连接读取数据失败，timer回调函数cb_func
//                     timer->cb_func(&user_timer[sockfd]);
//                     // printf("close connection...\n");
//                     // users[sockfd].close_conn();
//                     if(timer){
//                         timer_lst.del_timer(timer);
//                     }
//                 }
//             }
//             else if(events[i].events & EPOLLOUT){
//                 util_timer *timer = user_timer[sockfd].timer;
//                 if(users[sockfd].write()){
//                     // printf("write finished...\n");
//                     // users[sockfd].close_conn();
//                     if(timer){
//                         time_t cur = time(NULL);
//                         timer->expire = cur + 3 * TIMESLOT;
//                         timer_lst.adjust_timer(timer);
//                     }
//                 }
//                 else{
//                     timer->cb_func(&user_timer[sockfd]);
//                     if(timer){
//                         timer_lst.del_timer(timer);
//                     }
//                 }
//             }
//         }
//         if(timeout){
//             timer_handler();
//             timeout = false;
//         }
//     }
//     close(epollfd);
//     close(listenfd);
//     close(pipefd[0]);
//     close(pipefd[1]);
//     delete [] users;
//     delete [] user_timer;
//     delete pool;
//     return 0;
// }


#include "./config/config.h"
// #include "webserver.h"

int main(int argc, char *argv[]){
    //connPool->init("localhost", "jym", "1999", "mydb", 3306, 8);
    string user = "jym";
    string passwd = "1999";
    string dbname = "mydb";

    Config config;
    config.parse_arg(argc, argv);

    webserver server;

    server.init(config.port, user, passwd, dbname, config.logwrite, config.opt_linger, config.trigmode, config.sql_num, config.thread_num, config.close_log, config.actor_model, true);

    server.log_write();

    server.sql_pool();

    server.thread_pool();

    server.trig_mode();

    server.eventListen();

    server.eventLoop();

    return 0;
}