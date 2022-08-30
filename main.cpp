
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ecapi.h"
#ifdef LIBEXT
#include "libext.h"
#endif

int signal_6_doubleFree()
{
    char* str = (char*)calloc(4, 1);
    str[0] = 'A';
    free(str);
    free(str);
    printf("123");
    return 0;
}

int signal_8_fpe()
{
    int num = 0;
    return 128 / num;
}

int signal_11_segv()
{
    int* addr = (int*)10086;
    *addr = 10010;
    return 0;
}

void callback(int signal, const char* log, void* priv)
{
    printf("=== Crash by signal %d(%X) ===\r\n", signal, signal);
    printf("%s\r\n", log);

    /* 测试信号,不退出程序 */
    if (signal == SIGUSR1 || signal == SIGUSR2)
        ;
    /* 异常信号,务必在此退出程序,否则可能死循环 */
    else
        exit(0);
}

int main(void)
{
	/* 注册信号,使用0结尾 */
    ecapi_register(callback,  NULL, 0);

	/* 挑选一种 go die 方式 */
    signal_6_doubleFree();
    // signal_8_fpe();
    // signal_11_segv();
    // libext_test();
    // ecapi_signal_test(SIGUSR1);

    return 0;
}
