#include "heaptimer.h"

void HeapTimer::siftup_(size_t i){
    assert(i >= 0 && i < heap_.size());

    size_t j = (i - 1) / 2;
    while(j >= 0){
        if(heap_[j] < heap_[i]){
            break;
        }

        swapnode(i, j);
        i = j;
        j = (i - 1) / 2;
    }
}

void HeapTimer::siftdown_(size_t index, size_t n){
    assert(index >= 0 && index < heap_.size());
    assert(n >= 0 && n <= heap_.size());

    size_t i = index;
    size_t j = i * 2 + 1;

    while(j < n){
        if(j + 1 < n && heap_[j+1] < heap_[j])
            j++;
        if(heap_[i] < heap_[j])
            break;
        swapnode(i, j);

        i = j;

        j = i * 2 + 1;
    }
}

void HeapTimer::swapnode(size_t i, size_t j){
    assert(i >= 0 && i < heap_.size());
    assert(j >= 0 && j < heap_.size());

    std::swap(heap_[i], heap_[j]);
    ref_[heap_[i].id] = i;
    ref_[heap_[j].id] = j;
}

void HeapTimer::add_timer(int fd, int timeout, void(*cb_func)(int fd)){
    assert(fd >= 0);

    size_t i;
    if(ref_.count(fd) == 0){
        i = heap_.size();
        ref_[fd] = i;
        heap_.push_back({fd, time(NULL) + 3 * TIMESLOT, cb_func});
        siftup_(i);
    }

    else{
        i = ref_[fd];
        heap_[i].expires = time(NULL) + 3 * TIMESLOT;
        heap_[i].cb_func = cb_func;
        if(!siftdown_(i, heap_.size())){
            siftup_(i);
        }
    }
}

void HeapTimer::del_timer(size_t index){
    assert(!heap_.empty() && index >= 0 && index < heap_.size());

    size_t i = index;
    size_t n = heap_.size() - 1;

    assert(i <= n);

    if(i < n){
        swapnode(i, n);
        if(!siftdown_(i, n)){
            siftup_(i);
        }
    }

    ref_.erase(heap_.back().fd);
    heap_.pop_back();
}

void HeapTimer::adjust_timer(int fd, int timeout){
    assert(!heap_.empty() && ref_.count(id) > 0);

    heap_[ref_[id]].expires = time(NULL) + 3 * TIMESLOT;
    siftdown_(ref_[id], heap_.size());
}

void HeapTimer::tick(){
    if(heap_.empty()){
        return ;
    }

    while(!heap_.empty()){
        TimerNode node = heap_.front();
        if(node.expires - time(NULL) > 0){
            break;
        }

        node.cb_func(node.fd);
        del_timer(0);
    }
}

void HeapTimer::clear(){
    ref_.clear();
    heap_.clear();
}

void HeapTimer::dowork(int fd){
    if(heap_.empty() || ref_.count(fd) == 0){
        return ;
    }
    size_t i = ref_[fd];
    TimerNode node = heap_[i];
    node.cb_func(node.fd);
    del_timer(i);
}

TimerNode* HeapTimer::get_timer(int fd){
    assert(fd >= 0);

    if(heap_.empty() || ref_.count(fd) == 0){
        return nullptr;
    }

    size_t i = ref_[fd];
    return &heap_[i];
}

void cb_func_heap(int fd){
    assert(fd >= 0);
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
    http_conn::m_user_count--;
}