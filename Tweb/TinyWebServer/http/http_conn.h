#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
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
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
class http_conn
{
public:
    // 设置读取文件的名称 m_real_file大小
    static const int FILENAME_LEN = 200;
    // 设置读缓冲区 m_read_buf 大小
    static const int READ_BUFFER_SIZE = 2048;
    // 设置写缓冲区 m_write_buf大小
    static const int WRITE_BUFFER_SIZE = 1024;
    
    // 报文的请求方法
    enum METHOND
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH    
    };
    
    // 主状态机的状态
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESELINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT    
    };
    
    // 报文解析的结果
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNNECTION    
    };
    
    // 从状态机的状态
    enum LINE_STATUS
        {
            LINE_OK = 0,
            LINE_BAD,
            LINE_OPEN
        };

public:
    http_conn() {}
    ~http_conn() {}

public:
    // 初始化套接字地址，函数内部会调用私有方法init
    void init(int sockfd, const sockaddr_in &addr);
    // 关闭http连接
    void close_conn(bool real_close = true); 
    void process();
    // 读取浏览器端发来的全部数据
    bool read_once();
    // 响应报文写入函数
    bool write();
    sockaddr_in *get_address()
    {
        return &m_address;    
    }
    // 同步线程初始化数据库读取表
    void initmysql_result(connection_pool *connPool);


};



#endif
