#ifndef THREADPOOL_H
#define THREADPOLL_H

#include<pthread.h>
#include<vector>
#include<list>
#include<exception>
#include<cstdio>
#include "../locker/locker.h"
#include "../sql_conn/sql_connection_pool.h"

template<typename T>
class threadpool{
private:
    
    int m_thread_num;                   //线程的数量
    
    pthread_t *m_threads;               //线程池数组
    
    int m_max_request;                  //请求队列中最多允许的等待请求的数量
    
    std::list<T*> m_workqueue;          //请求队列
    
    locker m_queuelocker;               //互斥锁
    
    Connection_pool *m_connPool;        //数据库
    
    sem m_queuestat;                    //信号量，用来判断是否有任务需要处理

    int m_actor_model;                  //模型切换

    static void* worker(void * arg);    //工作线程运行的函数

    void run();

public:
    threadpool(int actor_model, Connection_pool *connPool, int thread_number = 8, int max_request = 10000);

    ~threadpool();

    bool append(T* request, int state);

    bool append_p(T *request);
};


template<typename T> 
threadpool<T>::threadpool(int actor_model, Connection_pool *connPool, int thread_number, int max_requests) : m_actor_model(actor_model), m_connPool(connPool), m_thread_num(thread_number), m_max_request(max_requests), m_threads(NULL){
    if((thread_number <= 0) || (max_requests <= 0)){
        throw std::exception();
    }
    
    m_threads = new pthread_t[m_thread_num];

    if(!m_threads){
        throw std::exception();
    }

    for(int i = 0; i < thread_number; i++){
        if(pthread_create(m_threads+i, NULL, worker, this) != 0){
            delete [] m_threads;
            throw std::exception();
        }

        if(pthread_detach(m_threads[i])){
            delete [] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool(){
    delete [] m_threads;
}

template<typename T>
bool threadpool<T>::append(T* request, int state){
    m_queuelocker.lock();
    if(m_workqueue.size() > m_max_request){
        m_queuelocker.unlock();
        return false;
    }

    request->m_state = state;

    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template<typename T>
bool threadpool<T>::append_p(T *request){
    m_queuelocker.lock();
    if(m_workqueue.size() > m_max_request){
        m_queuelocker.unlock();
        return false;
    }

    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template<typename T>
void *threadpool<T>::worker(void *arg){
    threadpool * pool = (threadpool *)arg;
    pool->run();
    return pool;
}

template<typename T>
void threadpool<T>::run(){
    while(true){
        m_queuestat.wait();
        m_queuelocker.lock();
        if(m_workqueue.empty()){
            m_queuelocker.unlock();
            continue;
        }

        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        if(!request){
            continue;
        }

        if(m_actor_model == 0){ //reactor
            if(request->m_state == 0){ //process read
                if(request->read()){ 
                    request->improv = 1;
                    ConnectionRAII mysqlconn(&request->mysql, m_connPool);
                    request->process();
                }
                else{
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            else{ //process write
                if(request->write()){
                    request->improv = 1;
                }
                else{
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        else{ //proactor
            ConnectionRAII mysqlconn(&request->mysql, m_connPool);
            request->process();
        }
    }
}

#endif