#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template <typename T>
class threadpool
{
public:
    // 定义初始化线程池中线程数量，以及最大连接数
    threadpool(connection_pool *connPool, int thread_number = 8, int max_request = 10000);

    ~threadpool();
    bool append(T *request);

private:
    // 工作线程运行的函数，它不断地从工作队列中取出任务并执行，它必须是静态成员函数
    static void *worker(void *arg);

    void run();
private:
    int m_thread_number; // 线程池中的线程数
    int m_max_requests; // 请求队列中允许的最大请求数
    pthread_t *m_threads; // 描述连接池的数组，其大小为m_thread_number
    std::list<T*> m_workqueue; // 请求队列
    locker m_queuelocker; // 保护请求队列的互斥锁
    sem m_queuestat; // 是否有任务需要处理
    bool m_stop; // 是否结束线程
    connection_pool *m_connPool; // 数据库
};

// 初始化线程池
template <typename T>
threadpool<T>::threadpool( connection_pool *connPool, int thread_number, int max_requests) : m_thread_number(thread_number), m_max_requests(max_requests), m_stop(false), m_threads(NULL),m_connPool(connPool)
{
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    m_threads = new pthread_t[m_thread_number]; // 初始化线程池数组
    if (!m_threads)
        throw std::exception();

    for (int i=0; i<thread_number; ++i)
    {
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete[] m_threads;
            throw std::exception();    
        }

        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads;    
            throw std::exception();    
        }
    }
    
}

// 销毁线程池
template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;    
    m_stop = true;
}

template <typename T>
bool threadpool<T>::append(T* request)
{
    m_queuelocker.lock();
    // 如果连接数大于最大连接数，则退出
    if (m_workqueue.size() > m_max_requests)
    {
        m_queuelocker.unlock();
        return false;    
    }
    // 添加任务
    m_workerqueue.push_back(request);
    m_queuelocker.unlock();
    
    // 信号量+1， 提醒有任务要处理
    m_queuestat.post();
    return true;
}

template <typename T>
void* threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool*)arg;
    pool->run();
    return pool;
}

template <typename T>
void threadpool<T>::run()
{
    while (!m_stop)
    {
        m_queuestat.wait();
        m_queuelocker.lock();
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();    
            continue;
        }    
        
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request)
            continue;
        
        connectionRAII mysqlcon(&request->mysql, m_connPool);
        request->process;
    }    
    
    
}
#endif
