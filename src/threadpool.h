#pragma once

#include <exception>
#include <pthread.h>
#include <list>
#include "locker.h"
#include <cstdio>
template<typename T>
class threadpool {
public:
    threadpool(int t_num = 8, int max_req = 10000);
    ~threadpool();
    bool append(T* request);
private:

    static void* worker(void *arg);
    void run();

private:
    int m_thread_number;
    //tid数组
    pthread_t *m_threads;
    //请求队列中最多允许的等待处理的请求数量
    int m_max_requests;
    //请求队列
    std::list< T*> m_workqueue;
    //互斥锁
    locker m_queuelocker;
    //信号量 判断是否有任务需要处理
    sem m_queuestat;
    //是否结束线程
    bool m_stop;
};

template<typename T>
threadpool<T>::threadpool(int t_num, int max_req) : m_thread_number(t_num), m_max_requests(max_req),m_stop(false), m_threads(NULL) {
    if (t_num <= 0 || max_req <= 0) {
        throw std::exception();
    }
    m_threads = new pthread_t[m_thread_number];
    if(!m_threads) {
        throw std::exception();
    }
    //创建thread_num个线程并设置线程分离
    for(int i = 0; i < m_thread_number; i++) {
        printf("create %d routine\n", i);
        if(pthread_create(m_threads + i, NULL, worker, this) < 0) {
            delete [] m_threads;
            throw std::exception();
        }
        if(pthread_detach(m_threads[i]) < 0) {
            delete[] m_threads;
            throw std::exception();
        }
    }

}

template<typename T>
threadpool<T>::~threadpool() {
    delete [] m_threads;
    m_stop = true;
}

template<typename T>
bool threadpool<T>::append(T *request) {
    m_queuelocker.Lock();
    if(m_workqueue.size() > m_max_requests) {
        m_queuelocker.Unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.Unlock();
    m_queuestat.Post();//增加信号量
    return true;

}

template<typename T>
void* threadpool<T>::worker(void* arg) {
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}
template<typename T>
void threadpool<T>::run() {
    while(!m_stop) {
        m_queuestat.Wait();
        m_queuelocker.Lock();
        if(m_workqueue.empty()) {
            m_queuelocker.Unlock();
            continue;
        }
        T* request = m_workqueue.front();//获取任务
        m_workqueue.pop_front();
        m_queuelocker.Unlock();
        if (!request) {
            continue;
        }
        request->Process();//开始
    }
}