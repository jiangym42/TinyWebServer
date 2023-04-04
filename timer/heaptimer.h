#ifndef HEAP_TIMER_H
#define HEAP_TIMER_H

#include<queue>
#include<unordered_map>
#include <time.h>
#include <algorithm>
#include <arpa/inet.h> 
#include <functional> 
#include <assert.h> 
#include<time.h>

#include "../log/log.h"
#include "../http_conn/http_conn.h"

const int TIMESLOT = 5;

struct TimerNode{
    int fd;
    time_t expires;
    void(*cb_func)(int fd);

    bool operator<(const TimerNode & t){
        return expires < t.expires;
    }
}

class HeapTimer{
public:
    HeapTimer(){
        heap_.reserve(64);
    }

    ~HeapTimer(){
        clear();
    }

    void adjust_timer(int fd, int newExpires);

    void add_timer(int fd, int timeout, void(*cb_func)(int fd));

    void del_timer(size_t i);

    void clear();

    void tick();

    void dowork(int fd);
    
    TimerNode* get_timer(int fd);

private:
    void siftup_(size_t i);
    
    void siftdown_(size_t index, size_t n);

    void swapnode(size_t i, size_t j);

    std::vector<TimerNode> heap_;

    std::unordered_map<int, size_t> ref_;
}

void cb_func(TimerNode *node);

#endif