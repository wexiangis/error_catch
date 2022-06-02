
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ecapi.h"
#ifdef LIBEXT
#include "libext.h"
#endif

int signal_6_abrt(int len)
{
    char* str = (char*)calloc(len, 1);
    // printf("%s(): 6 Abnormal termination (double free) \r\n", __FUNCTION__);
    str[0] = 'A';
    free(str);
    if (len)
        free(str);
    return len;
}

int signal_8_fpe(int num)
{
    // printf("%s(): 8 Erroneous arithmetic operation (floating point exception) \r\n", __FUNCTION__);
    return 128 / num;
}

int signal_11_segv(int addr)
{
    // printf("%s(): 11 Invalid access to storage \r\n", __FUNCTION__);
    memcpy((char*)&addr, "12345678", 8);
    return addr;
}

void callback(int signal, char** funs, int size, void* priv)
{
    int i = 0;

    printf("=== Crash by signal %d, backtrace function %d: ===\r\n", signal, size);
    for(i = 0; i < size; i ++)
        printf("  %02dth: %s \r\n", i, funs[i]);

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
    ecapi_register(
        callback,
        NULL,
        SIGINT,
        SIGILL,
        SIGABRT,
        SIGFPE,
        SIGSEGV,
        SIGTERM,
        SIGKILL,
        0);

	/* 挑选一种 go die 方式 */
    signal_6_abrt(8);
    // signal_8_fpe(0);
    // signal_11_segv(0);
    // libext_test(8);
    // ecapi_signal_test(SIGUSR1);

    return 0;
}
