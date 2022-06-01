# 捕捉崩溃信号并打印崩溃定位信息

## 1. 文件说明

* ecapi.c/h : 崩溃捕捉封装接口(error catch api)
* libext.c/h : 测试用动态库源码,模拟崩溃位置在动态库的情况
* main.c : 测试用源码

## 2. 原理简介

* 1). signal() 函数注册信号回调；
* 2). 在信号回调函数中,使用 backtrace() 函数获得最近函数跳转地址(系统内存地址);
* 3). 解析 /proc/self/maps 匹配所在系统内存起始地址,跳转地址减去该值即为在执行文件内的地址;
* 4). 解析 /proc/self/maps 地址对应的源文件,通过elf格式解析,匹配符号地址得到对应的函数名称.

## 3. Example
```shell
$user@ubuntu: make && ./out
free(): double free detected in tcache 2
=== Crash by signal 6, backtrace function 11: ===
  00th: 00002298 ecapi_signal /disk/pro/github/error_catch/out 
  01th: 000430C0 killpg /usr/lib/x86_64-linux-gnu/libc-2.31.so 
  02th: 0004303B gsignal /usr/lib/x86_64-linux-gnu/libc-2.31.so 
  03th: 00022859 _end /usr/lib/x86_64-linux-gnu/libc-2.31.so 
  04th: 0008D29E __fsetlocking /usr/lib/x86_64-linux-gnu/libc-2.31.so 
  05th: 0009532C pthread_attr_setschedparam /usr/lib/x86_64-linux-gnu/libc-2.31.so 
  06th: 00096F9D pthread_attr_setschedparam /usr/lib/x86_64-linux-gnu/libc-2.31.so 
  07th: 000013D3 signal_6_abrt /disk/pro/github/error_catch/out 
  08th: 000014EA main /disk/pro/github/error_catch/out 
  09th: 000240B3 __libc_start_main /usr/lib/x86_64-linux-gnu/libc-2.31.so 
  10th: 000012CE _start /disk/pro/github/error_catch/out 
```
* 上图打印为最后调用的函数列表信息
* 函数调用顺序从下往上
* 虽然 ecapi_signal 是最后一个函数,但实际崩溃位置在 signal_6_abrt 对应源程序文件 out
