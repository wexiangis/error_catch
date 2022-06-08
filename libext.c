#include <stdio.h>
#include <stdlib.h>

static int libext_6_doubleFree()
{
    char* str = (char*)calloc(4, 1);
    str[0] = 'A';
    free(str);
    free(str);
    return 0;
}

void libext_test()
{
    libext_6_doubleFree();
    printf("call libext_test\r\n");
}
