#pragma once
#include <bits/types/struct_timespec.h>
#include <exception>
#include <pthread.h>
#include <semaphore.h>
class locker {
public:
    locker() {
        if(pthread_mutex_init(&m_mutex, NULL) != 0) {
            throw std::exception();
        }
    }

    ~locker() {
        pthread_mutex_destroy(&m_mutex);
    }
    bool Lock() {
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    bool Unlock() {
        return pthread_mutex_unlock(&m_mutex);
    }
    pthread_mutex_t* Get()  {
        return &m_mutex;
    }
private:
    pthread_mutex_t m_mutex;
};
// cond 条件变量
class cond {
public:
    cond() {
        if(pthread_cond_init(&m_cond, NULL) != 0) {
            throw std::exception();
        }
    }
    ~cond() {
        pthread_cond_destroy(&m_cond);
    }
    bool Wait(pthread_mutex_t *mutex) {
        return pthread_cond_wait(&m_cond, mutex) == 0;
    }
    bool TimedWait(pthread_mutex_t *mutex, struct timespec t) {
        return pthread_cond_timedwait(&m_cond, mutex, &t) == 0;
    }
    bool Signal(pthread_mutex_t *mutex) {
        return pthread_cond_signal(&m_cond) == 0;
    }
    bool BroadCast() {
        return pthread_cond_broadcast(&m_cond) == 0;
    }
private:
    pthread_cond_t m_cond;
};

// sem 信号量
class sem {
public:
    sem() {
        if(sem_init(&m_sem, 0, 0) != 0) {
            throw std::exception();
        }
    }
    sem(int num) {
        if(sem_init(&m_sem, 0, num) != 0) {
            throw std::exception();
        }
    }
    ~sem() {
        sem_destroy(&m_sem);
    }

    bool Wait() {
        return sem_wait(&m_sem) == 0;
    }
    bool Post() {
        return sem_post(&m_sem);
    }
private:
    sem_t m_sem;
};