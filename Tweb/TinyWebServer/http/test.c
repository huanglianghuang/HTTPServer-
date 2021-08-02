#include <stdio.h>
#include <stdarg.h>
//int my_snprintf(char *s, int size, const char *fmt, ...) //该自定义函数，与系统提供的snprintf()函数相同。
//{
//    va_list ap;
//    int n=0;
//    va_start(ap, fmt); //获得可变参数列表
//    n=vsnprintf (s, size, fmt, ap); //写入字符串s
//    va_end(ap); //释放资源
//    return n; //返回写入的字符个数
//}
//int main() {
//    char str[1024];
//    int  n = my_snprintf( str, sizeof(str), "%d,%s,%d,%d",5,"abcde",7,8);
//    printf("写入的字符串个数 =%d, 字符串为：%s\n",n,str);
//    return 0;
//}

//int add_response(char* m_write_buf, char *format, ...)
//{
//    int m_write_idx = 0;
//    va_list arg_list;
//    va_start(arg_list, format);
//    int len = vsnprintf(m_write_buf, 1024, format, arg_list);
//    va_end(arg_list);
//    return len;
//}
//
//int main()
//{
//    char *error_500_title = "Internal Error";
//    char str[1024];
//    int status = 500;
//    int len = add_response(str, "%s %d %s\r\n", "HTTP/1.1", status, "Internal Error");    
//    printf("%d\n",len);
//    printf("%s\n", str);
//    return 0;
//}
int main()
{
    char new_log[256]={0};
    char dir[2];
    char log[3];
    char test[10]={'a','b','v'};
    snprintf(new_log, 255, "%s\n",  test);
    printf("%s",new_log);
    FILE* m_fp;
    m_fp = fopen(new_log, "a");
    fputs("this is test", m_fp);
    return 0;    
}
