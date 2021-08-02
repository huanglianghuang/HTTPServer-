#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

class sem
{
public:
	sem() // 默认构造函数
	{
		if (sem_init(&m_sem, 0, 0) != 0)
		{
			throw std::exception();
		}	

	}
    sem(init num) // 有参构造函数
    {
		if (sem_init(&m_sem, 0, num) != 0) // 设置 m_sem信号量初始值为num
		{
			throw std::exception();
		}	
    }

    ~sem()
    {
        sem_destroy(&m_sem);
    }

    bool wait()  // 信号量减1
    {
        return sem_wait(&m_sem) == 0;
    }

    bool post() // 信号量加1
    {
        return sem_post(&m_sem) == 0;
    }
private:
	sem_t m_sem;
};

class locker
{
public:
    locker()
    {
        if (pthread_mutex_init(&m_mutex, NULL) != 0)
        {
            throw std::exception();
        }
    }

    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);
    }

    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    
    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

    pthread_mutex_t *get()
    {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};

class cond
{
public:
    cond()
    {
        if (pthread_cond_init(&m_cond, NULL) != 0)
        {
            throw std::exception();
        }
    }
    ~cond()
    {
        pthread_cond_destroy(&m_cond);
    }

    bool wait(pthread_mutex_t *m_mutex)
    {
        return pthread_cond_wait(&m_cond, m_mutex) == 0;
    }

    bool timewait(pthread_mutex_t *m_mutex, struct timespec t)
    {
        return pthread_cond_timewait(&m_cond, m_mutex, &t) == 0;
    }

    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0;
    }

    bool broadcast(0
    {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    pthread_cond_t m_cond;
};

#endif
