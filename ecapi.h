#ifndef _EC_API_H_
#define _EC_API_H_

#include <stdarg.h>
#include <signal.h>

/*
 *  崩溃前回调
 *  param:
 *      signal: 崩溃信号
 *      positionList: 最后调用的函数列表
 *      positionNum: 列表长度
 */
typedef void (*ECAPI_CALLBACK)(int signal, char** positionList, int positionNum, void* priv);

/*
 *  推荐注册信号:
 *      SIGINT, SIGILL, SIGABRT, SIGFPE, SIGSEGV, SIGTERM, SIGKILL
 *  
 *  示例: (边长参数用0结尾)
 *      ecapi_register(callback, NULL, SIGINT, 0);
 *      ecapi_register(callback, NULL, SIGINT, SIGILL, 0);
 *      ecapi_register(callback, NULL, SIGINT, SIGILL, SIGABRT, 0);
 * 
 *  补充:
 *      API里面不做程序退出操作,如需要,可在 callback 回调里放置 exit(0)
 */
void ecapi_register(ECAPI_CALLBACK callback, void* priv, int signal, ...);

/*
 *  信号测试,会触发上面的回调
 */
void ecapi_signal_test(int sig);

#endif // _EC_API_H_
