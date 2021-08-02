// 基于升序双向链表的定时器，并按照超时时间进行排序
#ifndef LST_TIMER
#define LST_TIMER
#include<time.h>
#include"../log/log.h"

class util_timer;

// 保存客户端用户相关数据，包括地址，客户端套接字以及定时器
struct client_data
{
    sockaddr_in address;
    int sockfd;
    util_timer *timer; // 定时器
}

// 定时器类(类似于定义了链表节点)
class util_timer
{
    public:
        util_timer() : prev(NULL), next(NULL) {}

    public:
        time_t expire; /*任务的超时时间*/

        void (*cb_func)(client_data *); // 定时器任务回调函数，在本项目中，回调函数的任务是删除非活跃连接

        client_data *user_data; // 回调函数所需处理的任务数据
        util_timer *prev; // 定时器的前向指针
        util_timer *next; // 定时器的后向指针
}

// 定时器双向链表（类似于具体实现链表）
class sort_timer_lst
{
    public:
        sort_timer_lst() : head(NULL), tail(NULL) {}
        ~sort_timer_lst()
        {
            util_timer *tmp = head;
            while (tmp)
            {
                head = tmp->next;
                delete tmp;
                tmp = head;
            }
        }
        // 在链表中加入定时器
        void add_timer(util_timer *timer)
        {
            if (!timer)
            {
                return;
            }
            // 如果链表没有头结点，说明此时加入的定时器就是头结点
            if (!head)
            {
                head = tail = timer;
                return;
            }
            // 如果加入的定时器的超时时间小于头结点，则将加入的定时器设为头结点，并将原来的头结点后移
            if (timer->expire < head->expire)
            {
                timer->next = head;
                head->prev = timer;
                head = timer;
                return;
            }
            // 其他情况调用辅助函数，确保链表按照定时器超时时间升序排列
            add_timer(timer, head);
        }
        // 调整链表中定时器顺序，确保按照升序排列 
        void adjust_timer(util_timer *timer)
        {
            if (!timer)
            {
                return;
            }
            // 记录需要调整的定时器的下一个节点
            util_timer *tmp = timer->next;
            if (!tmp || (timer->expire < tmp->expire)) // 当前定时器超时时间小于下一个定时器，则说明不用调整
            {
                return;
            }
            // 如果需要修改的定时器刚好是头结点，则先取出头结点，再根据超时时间重新插入链表中，确保升序排列
            if (timer == head)
            {
                head = head->next;
                head->prev = NULL;
                timer->next = NULL;
                add_timer(timer, head);
            }
            // 如果不是头结点，则取出该节点，再重新按照顺序插入
            else
            {
                timer->prev->next = timer->next;
                timer->next->prev = timer->prev;
                add_timer(timer, timer->next);
            }
        }
        // 如果某个连接超时了，则将其从链表中删除(和普通的链表删除操作一样)
        void del_timer(util_timer *timer)
        {
            if (!timer)
            {
                return;
            }
            if ((timer == head) && (timer == tail))
            {
                delete timer;
                head = NULL;
                tail = NULL;
                return;
            }
            if (timer == head)
            {
                head = head->next;
                head->prev = NULL;
                delete timer;
                return;
            }
            if (timer == tail)
            {
                tail = tail->prev;
                tail->next = NULL;
                delete timer;
                return;
            }
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            delete timer;
        }
        /* SIGALRM信号每触发一次就在其信号处理函数（如果使用统一事件源，则是主函数）中执行一次tick函数，以处理链表上到期的任务*/
        void tick()
        {
            if (!head)
            {
                return;
            }
            //printf( "timer tick\n" );
            LOG_INFO("%s", "timer tick");
            Log::get_instance()->flush();
            // 记录当前时间，以便和链表中定时器超时时间进行比较，如果超时了，则调用回调函数，删除该节点
            time_t cur = time(NULL);
            util_timer *tmp = head;

            // 从头节点开始处理每个定时器，遇到超时的定时器，则将其删除
            while (tmp)
            {
                // 如果当前时间小于头结点时间，那肯定没有超时（过时），直接退出即可
                if (cur < tmp->expire)
                {
                    break;
                }
                // 如果头结点时间小于当前时间，那肯定超时了（节点时间不能超过当前时间，确保其在当前时间之后）
                tmp->cb_func(tmp->user_data); // 超时了先执行定时器超时事件
                // 删除超时的节点
                head = tmp->next;
                if (head)
                {
                    head->prev = NULL;
                }
                delete tmp;
                tmp = head;
            }
        }
    private:
        // 从头结点开始，将定时器（既是链表的节点）插入到适当的位置
        void add_timer(util_timer *timer, util_timer *lst_head)
        {
            util_timer *prev = lst_head;
            util_timer *tmp = prev->next;
            while (tmp)
            {
                if (timer->expire < tmp->expire)
                {
                    prev->next = timer;
                    timer->next = tmp;
                    tmp->prev = timer;
                    timer->prev = prev;
                    break;
                }
                prev = tmp;
                tmp = tmp->next;
            }
            // 说明只有一个 头结点
            if (!tmp)
            {
                prev->next = timer;
                timer->prev = prev;
                timer->next = NULL;
                tail = timer;
            }
        }
        util_timer *head; // 链表头节点
        util_timer *tail; // 链表尾节点
}
#endif
