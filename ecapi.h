#ifndef _EC_API_H_
#define _EC_API_H_

#include <stdarg.h>
#include <signal.h>

/*
 *  callback before crash
 *  param:
 *      signal: crash signal
 *      positionList: crash position list
 *      positionNum: positionList[positionNum]
 */
typedef void (*ECAPI_CALLBACK)(int signal, char** positionList, int positionNum, void* priv);

/*
 *  recommend: (register all of them)
 *      SIGINT, SIGILL, SIGABRT, SIGFPE, SIGSEGV, SIGTERM, SIGKILL
 *  
 *  Example:
 *      ecapi_register(callback, NULL, SIGINT, 0);
 *      ecapi_register(callback, NULL, SIGINT, SIGILL, 0);
 *      ecapi_register(callback, NULL, SIGINT, SIGILL, SIGABRT, 0);
 */
void ecapi_register(ECAPI_CALLBACK callback, void* priv, int signal, ...);

#endif // _EC_API_H_
