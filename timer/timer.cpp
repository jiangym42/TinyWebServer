#include "timer.h"

void sort_util_timer::add_timer(util_timer *timer, util_timer *lst_head){
    util_timer *prev = lst_head;
    util_timer *tmp = prev->next;

    while(tmp){
        if(timer->expire < tmp->expire){
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }

    if(!tmp){
        prev->next = timer;
        timer->prev = prev;
        timer->next = nullptr;
        tail = timer;
    }
}

void sort_util_timer::add_timer(util_timer *timer){
    if(!timer){
        return;
    }
    if(!head){
        head = tail = timer;
        return;
    }
    if(timer->expire < head->expire){
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }

    add_timer(timer, head);
}

void sort_util_timer::adjust_timer(util_timer *timer){
    if(!timer){
        return;
    }

    util_timer *tmp = timer->next;
    if(!tmp || timer->expire < tmp->expire){
        return;
    }

    if(head == timer){
        head = head->next;
        head->prev = nullptr;
        timer->next = nullptr;
        add_timer(timer, head);
    }
    else{
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}

void sort_util_timer::del_timer(util_timer *timer){
    if(!timer){
        return;
    }
    //链表中只有当前timer一个节点
    if((timer == head) && (timer == tail)){
        delete timer;
        head = nullptr;
        tail = nullptr;
        return;
    }
    //timer就是当前链表的头结点
    if(timer == head){
        head = head->next;
        head->prev = nullptr;
        delete timer;
        return;
    }
    //timer是当前链表的尾节点
    if(timer == tail){
        tail = tail->prev;
        tail->next = nullptr;
        delete timer;
        return;
    }

    //timer在链表中间
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}

void sort_util_timer::tick(){
    if(!head){
        return;
    }

    time_t cur = time(NULL);
    util_timer *tmp = head;
    while(tmp){
        if(cur < tmp->expire){
            break;
        }
        tmp->cb_func(tmp->user_data);
        head = tmp->next;
        if(head){
            head->prev = nullptr;
        }

        delete tmp;
        tmp = head;
    }
}

void Utils::init(int timeslot){
    m_timeslot = timeslot;
}

int Utils::setnonblocking(int fd){
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
    return old_flag;
}

void Utils::addfd(int epollfd, int fd, bool one_shot, int trigmode){
    epoll_event event;
    event.data.fd = fd;
    if(trigmode == 1){
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    }
    else{
        event.events = EPOLLIN | EPOLLRDHUP;
    }

    if(one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void Utils::sig_handler(int sig){
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

void Utils::addsig(int sig, void(handler)(int), bool restart){
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if(restart){
        sa.sa_flags |= SA_RESTART;
    }

    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

void Utils::timer_handler(){
    m_timer_lst.tick();
    alarm(m_timeslot);
}

void Utils::show_error(int connfd, const char *info){
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;
void cb_func(client_data *user_data){
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
}