#include "http_conn.h"
#include "../log/log.h"
#include <map>
#include <mysql/mysql.h>
#include <fstream>

//#define connfdET //边缘触发非阻塞
#define connfdLT //水平触发阻塞

//#define listenfdET //边缘触发非阻塞
#define listenfdLT //水平触发阻塞

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

//当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
const char *doc_root = "/home/qgy/github/TinyWebServer/root";

//将表中的用户名和密码放入map
map<string, string> users;
locker m_lock;

void http_conn::initmysql_result(connection_pool *connPool)
{
    //先从连接池中取一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    //在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

int setnonblocking(int flag)
{
    int old_option = fctnl(flag, GETFL); // fcntl()函数，对已打开的文件描述符进行操作，已改变已打开的文件属性
    int new_option = old_option | O_NONBLOCK;  // 设置非阻塞的意思是，read和write函数不会阻塞在输入或者输出哪里等待。如果没有输入输出则直接跳过
    fctnl(fd, SETFL, new_option);
    return old_version;    

}

// 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;
#ifdef connfdET
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif

#ifdef connfdLT
    event.events = EPOLLIN | EPOLLRDHUP;
#endif

#ifdef listenfdET
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif

#ifdef listenfdLT
    event.events = EPOLLIN | EPOLLRDHUP;
#endif

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, %event);
    setnonblocking(fd);

}

// 从内核时间表删除文件描述符
void removed(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);    
}

// 将事件重置为EPOLLONESHOT，这样可以保证当前线程处理完socket之后，下一次这个socket发生可读事件，其他线程有机会拿到这个socket继续处理
void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
#ifdef connfdET // 如果是边缘触发
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
#endif

#ifdef connfdLT // 如果是水平触发
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
#endif

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);  
}
int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

// 关闭连接， 关闭一个连接，客户总量减1
void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;    
    }    
}

// 初始化连接，外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in& addr)
{
    m_sockfd = sockfd;
    m_address = addr;
    /*下面两行是为了避免TIME_WAIT状态，仅用于调试*/
    //int reuse=1;
    //setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    addfd(m_epollfd, sockfd, true);
    m_user_count++;
    init();    
}

//初始化新接受的连接
//check_state默认为分析请求行状态
void http_conn::init()
{
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

/*
   从状态机，用于分析出一行内容（包括请求行和请求头部），遇到空行（HTTP请求报文以空行区分正文和请求行和请求头部）就说明读取到完整的一行
   返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
LINE_OK:表示读取到一个完整的行
LINE_BAD: 说明读取出错
LINE_OPEN:表示读取的行数据尚不完整，需要继续读取
 */

/* 每次解析出一行，HTTP请求报文无论是请求行或者是请求头部，都会有一个\r\n字段,因此每次解析出一行来分析*/
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    // m_read_idx指在缓冲区已经读到的字节数，m_checked_idx指已经检查的字节数
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r') // 如果检测到回车键，则有可能是空行(\r\n)
        {
            if ((m_checked_idx+1) == m_read_idx) 
            {
                return LINE_OPEN;    
            }
            /*下面这个else if写的有点多余，因为肯定不会执行到这里无论\r后面是\n或者是字符串，肯定有(m_checked_idx+1) == m_read_idx这个
              关系, 也就是说总会执行上面的if语句，然后返回LINE_OPEN*/
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        // 上面的if已经检测到了\r,如果下面这个判断语句检测到了\n，那就说明已经读取到完整的一行了
        else if (temp == '\n')
        {
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;    
        }

    }

    return LINE_OPEN;
}

//循环读取客户数据，直到无数据可读或对方关闭连接
//非阻塞ET工作模式下，需要一次性将数据读完
bool http_conn::read_once()
{
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }
    int bytes_read = 0;

#ifdef connfdLT

    bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
    m_read_idx += bytes_read;

    if (bytes_read <= 0)
    {
        return false;
    }

    return true;

#endif

#ifdef connfdET
    while (true)
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            return false;
        }
        else if (bytes_read == 0)
        {
            return false;
        }
        m_read_idx += bytes_read;
    }
    return true;
#endif
}

//解析http请求行，获得请求方法，目标url及http版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{

    /* 假设请求行为 GET /1234.html HTTP/1.1 */

    /*C 库函数 char *strpbrk(const char *str1, const char *str2) 检索字符串 str1 中第一个匹配字符串 str2 中字符的字符，不包含空结束字符。也就是说，依次检验字符串 str1 中的字符，当被检验字符在字符串 str2 中也包含时，则停止检验，并返回该字符位置。*/
    m_url = strpbrk(text, " \t");// 遇到第一个空格停止
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0'; // 此时m_url指向空格的下一个位置，也就是反斜杠 /
    char *method = text; //取出方法
    if (strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        cgi = 1;
    }
    else
        return BAD_REQUEST;
    /*C 库函数 size_t strspn(const char *str1, const char *str2) 检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标。*/
    m_url += strspn(m_url, " \t"); // 此时m_url指向反斜杠
    m_version = strpbrk(m_url, " \t"); // 此时m_version指向第二个空格，也就是HTTP/1.1前面的空格
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t"); 
    if (strcasecmp(m_version, "HTTP/1.1") != 0)// 解析出HTTP版本号，仅支持HTTP/1.1
        return BAD_REQUEST;

    /*C 库函数 char *strchr(const char *str, int c) 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置。*/
    //这里主要是有些报文的请求资源中会带有 http://，这里需要对这种情况进行单独处理
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/'); 
    }

    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    //当url为 / 时，显示判断界面.即在访问该服务器时，通常输入 ip:port, 这种输入格式的请求行为GET / HTTP/1.1，即默认情况下是让服务器显示judge.html页面
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");
    m_check_state = CHECK_STATE_HEADER; //请求行解析完后要解析请求头部了
    return NO_REQUEST; // 表示请求不完整，要继续读取
}

//解析http请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    // 如果text[0] = '\0',则说明该行为空行
    if (text[0] == '\0')
    {
        // 如果m_content_length(描述请求主体长度) != 0，则说明是POST请求。因为GET请求的消息主体通常为0
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT; //POST请求则去解析消息主体内容
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true; //如果是长连接，则将linger标志设置为true
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
        //printf("oop!unknow header: %s\n",text);
        LOG_INFO("oop!unknow header: %s", text);
        Log::get_instance()->flush();
    }
    return NO_REQUEST;
}

//判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    // 如果已经读取的数据大于报体长度+已经检测的数据，则说明报体数据读完了，即一个完整的HTTP请求读完了
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0'; // POST请求中的报体是输入的用户名和密码，将其保存起来
        //POST请求中最后为输入的用户名和密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK)) // 先不断地读取一行，读取完一行后就进行处理,第一次读取完请求行后应该就去处理请求头部，
    {
        text = get_line(); // text指针指向某一行的开头，HTTP请求由很多行组成
        m_start_line = m_checked_idx; // 记录第几行
        LOG_INFO("%s", text);
        Log::get_instance()->flush();
        switch (m_check_state)
        {
            case CHECK_STATE_REQUESTLINE: 
                {
                    ret = parse_request_line(text);
                    if (ret == BAD_REQUEST)
                        return BAD_REQUEST;
                    break;
                }
            case CHECK_STATE_HEADER: 
                {
                    ret = parse_headers(text);
                    if (ret == BAD_REQUEST)
                        return BAD_REQUEST;
                    else if (ret == GET_REQUEST)
                    {
                        return do_request();
                    }
                    break;
                }
            case CHECK_STATE_CONTENT:
                {
                    ret = parse_content(text);
                    if (ret == GET_REQUEST)
                        return do_request();
                    line_status = LINE_OPEN;
                    break;
                }
            default:
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy(m_real_file, doc_root); // 将doc_root所指的字符串拷贝给m_read_file
    int len = strlen(doc_root);
    //printf("m_url:%s\n", m_url);
    // C 库函数 char *strrchr(const char *str, int c) 在参数 str 所指向的字符串中搜索最后一次出现字符 c（一个无符号字符）的位置。
    const char *p = strrchr(m_url, '/'); // m_url是parse_request_line()函数中取出的url资源，已经转化成字符串了

    //如果是POST请求则处理cgi
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {

        //根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        //将用户名和密码提取出来
        //user=123&passwd=123
        char name[100], password[100];
        int i;
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        //同步线程登录校验
        if (*(p + 1) == '3')
        {
            //如果是注册，先检测数据库中是否有重名的
            //没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if (users.find(name) == users.end())
            {

                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();

                if (!res)
                    strcpy(m_url, "/log.html");
                else
                    strcpy(m_url, "/registerError.html");
            }
            else
                strcpy(m_url, "/registerError.html");
        }
        //如果是登录，直接判断
        //若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (*(p + 1) == '2')
        {
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(m_url, "/welcome.html");
            else
                strcpy(m_url, "/logError.html");
        }
    }
    // 如果url中的下一个字符是0（对应的HTTP请求行为 POST /0 HTTP/1.1），则显示注册界面z
    if (*(p + 1) == '0')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real)); // len是doc_root的路径， doc_root = "/home/qgy/github/TinyWebServer/root";

        free(m_url_real);
    }
    else if (*(p + 1) == '1')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '5')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '6')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '7')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 如果都不是则说明是初始界面，此时 m_yrl为  / 
    else
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;
    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;
    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}
void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;    
    }    
}

//服务器子线程调用process_write完成响应报文，随后注册epollout事件。服务器主线程检测写事件，并调用http_conn::write函数将响应报文发送给浏览器端。
bool http_conn::write()
{
    int temp = 0;
    //若要发送的数据长度为0
    //表示响应报文为空，一般不会出现这种情况
    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init()
            return true;
    }
    while (1)
    {
        temp = write(m_sockfd, m_iv, m_iv_count);

        if (temp < 0)
        {
            // 这个是对方关闭连接了，但是缓冲区还有数据继续读，一般没有这种情况
            if (errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;    
            }
            unmap();
            return false;
        }

        // 已经发送的数据
        bytes_have_send += temp;
        // 剩余未发送的数据
        bytes_to_send -= temp;
        // 如果已经发送的数据超过的iv[0]数组的大小，则说明响应信息已经发送完了，接下来要发送正文消息了
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            // 将iv[0]数组置零，防止重复发送响应头信息
            m_iv[0].iov_len = 0;
            // 更新iv[1]数组指针指向，防止重复发送
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            // 如果发送的数据没有超过iv[0]数组的大小，则继续上次的位置发送
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        // 数据全部发送完毕
        if (bytes_to_send <= 0)
        {
            // 解除映射
            unmap();
            // 重新注册事件
            modfd(m_epollfd, m_sockfd, EPOLLIN);

            if (m_linger)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }

}
bool http_conn::add_response(const char *format, ...)
{
    // m_write_idx是缓冲区中的长度
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;

    /*
       int vsnprintf(char *str, size_t size, const char *format, va_list ap);
       可变参数列表va_list宏说明：
       void va_start(va_list ap, last); 
       void va_end(va_list ap);
       va_start与va_end是成对被调用的。

       简单地说，这个函数就是将字符串格式format存储在可变参数列表ap中，再将字符串按照ap中存储的输出格式format写入到缓冲区str中。
       就是按照一定的格式写入到缓冲区中，例如 snprintf( str, sizeof(str), "%d,%s,%d,%d",5,"abcde",7,8)，将 5,"abcde",7,8按照%d,%s,%d,%d的
       格式写入到str中，这个函数可以将不同的数据类型的数据写入到一个缓冲区中
     */
    va_list arg_list;    
    va_start(arg_list, format);
    // m_write_idx初始化为0，也就是从头开始写入到缓冲区中
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    // 如果写入到缓冲区中的数据长度len超过了缓冲区的大小，则错误返回
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        va_end(arg_list);
        return false;
    }

    // 更新m_write_idx的大小，意味着下一次写入时从m_write_idx的位置写入，而不是从头写入
    m_write_idx += len;
    va_end(arg_list);
    LOG_INFO("request:%s", m_write_buf);
    Log::get_instance()->flush();
    return true;
}

// 根据响应，将相应的HTTP 响应状态行 写入到m_write_buf中。状态行由三部分组成：协议版本，状态码，状态码描述。
bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);// status为状态码，title为状态码描述
}

// 将 响应头部 信息写入到m_write_buf 中
bool http_conn::add_headers(int content_len)
{
    add_content_length(content_len); 
    add_linger();
    add_blank_line();
}
// 将消息内容长度信息写入到m_write_buf中
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}
// 将格式信息写入
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}
// 添加连接状态，通知浏览器端是保持连接还是关闭
bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
// 添加空白行
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}
// 写入请求正文
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}

bool http::process_write(HTTP_CODE ret)
{
    switch(ret)
    {
        // 如果是网络错误
        case INTERNAL_ERROR:
            {
                add_status_line(500, error_500_title); // 在HTTP响应状态行添加网络错误字段，添加后为 HTTP/1.1 500 error_500_title 
                add_headers(strlen(error_500_form)); // 在HTTP响应头部添加网络错误对应的字段
                if (!add_content(error_500_form))
                    return false;
                break;        
            }

        case BAD_REQUEST:
            {
                add_status_line(404, error_404_title);
                add_headers(strlen(error_404_form));
                if (!add_content(error_404_form))
                    return false;
                break;    
            }

        case FORBIDDEN_REQUEST:
            {
                add_status_line(403, error_403_title);
                add_headers(strlen(error_403_form));
                if (!add_content(error_403_form))
                    return false;
                break;
            }
            // 如果是FILE_REQUEST，则说明可以正常响应
        case FILE_REQUEST:
            {
                add_status_line(200, ok_200_title);
                // m_file_stat是记录html文件资源的目录，这个目录的字符串被保存在m_real_file中
                if (m_file_stat.st_size != 0) // 如果文件内容大小不为0，说明文件存在且有效
                {
                    add_headers(m_file_stat.size); // 在响应头部字段content-length加入具体的长度
                    m_iv[0].iov_base = m_write_buf; // m_iv[0]数组只发生响应状态行和响应头部信息
                    m_iv[0].iov_len = m_write_idx; // m_iv[0]数组中要发生的数据长度

                    m_iv[1].iov_base = m_file_address; //m_iv[1]数组只发送消息正文。m_file_address保存了html文件的位置，它已经由mmp函数开辟了一段空间来保存
                    m_iv[1].iov_len = m_file_stat.st_size;
                    m_iv_count = 2;

                    bytes_to_send = m_write_idx + m_file_stat.st_size; //剩余发送字节数个大小

                }
                else
                {
                    //如果请求的资源大小为0，则返回空白html文件
                    const char*ok_string =  "<html><body></body></html>";
                    add_headers(strlen(ok_string));
                    if (!add_content(ok_string))
                        return false;    

                }

            }
        default:
            return false;

    }
    // 如果不是正确的FILE_REQUEST，则说明出错了，只返回相应的HTTP响应信息。没有资源请求
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;    

}

// 由线程池中的工作线程调用，这里是处理HTTP请求的入口函数
void http_conn::process()
{
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    bool write_ret = process_write(read_ret);
    if (!write_ret)
    {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}
