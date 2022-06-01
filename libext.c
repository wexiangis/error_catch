#include <stdio.h>
#include <stdlib.h>

static int libext_6_signal(int len)
{
    char* str = (char*)calloc(len, 1);
    // printf("%s(): 6 Abnormal termination (double free) \r\n", __FUNCTION__);
    str[0] = 'A';
    free(str);
    if (len)
        free(str);
    return len;
}

void libext_test(int i)
{
    libext_6_signal(i + i);
}
