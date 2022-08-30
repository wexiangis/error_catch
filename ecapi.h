#ifndef _EC_API_H_
#define _EC_API_H_

#include <stdarg.h>
#if defined(WIN) || defined(WIN32) || defined(WINCE) || defined(_MSC_VER)
#else
#include <signal.h>
#endif

/*
 *  崩溃前回调
 *  param:
 *      signal: 崩溃信号
 *      log: 日志信息
 */
typedef void (*ECAPI_CALLBACK)(int signal, const char* log, void* priv);

/*
 *  注册崩溃回调和要响应的信号
 *  参数:
 *     callback: 崩溃后回调,如果不是测试信号,务必在函数内调用 exit(-1) 结束进程
 *     priv: 私有传参
 *     signal: 注册要响应的信号,以0结尾. 若第一个 signal=0 则使用默认配置
 * 
 *  推荐注册信号:
 *      SIGINT, SIGILL, SIGABRT, SIGFPE, SIGSEGV, SIGTERM, SIGKILL
 *  
 *  示例: (边长参数用0结尾)
 *      ecapi_register(callback, NULL, 0); // 将自动填充
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
