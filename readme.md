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
  00th: 000015F3(00001531+000000C2) - ecapi_signal - /disk/pro/github/error_catch/out 
  01th: 000430C0(00043080+00000040) - killpg - /usr/lib/x86_64-linux-gnu/libc-2.31.so 
  02th: 0004303B(00042F70+000000CB) - gsignal - /usr/lib/x86_64-linux-gnu/libc-2.31.so 
  03th: 00022859(00005040+0001D819) - _end - /usr/lib/x86_64-linux-gnu/libc-2.31.so 
  04th: 0008D29E(0008CE70+0000042E) - __fsetlocking - /usr/lib/x86_64-linux-gnu/libc-2.31.so 
  05th: 0009532C(00094DE0+0000054C) - pthread_attr_setschedparam - /usr/lib/x86_64-linux-gnu/libc-2.31.so 
  06th: 00096F9D(00094DE0+000021BD) - pthread_attr_setschedparam - /usr/lib/x86_64-linux-gnu/libc-2.31.so 
  07th: 000013D3(00001389+0000004A) - signal_6_abrt - /disk/pro/github/error_catch/out 
  08th: 000014EA(000014A0+0000004A) - main - /disk/pro/github/error_catch/out 
  09th: 000240B3(00023FC0+000000F3) - __libc_start_main - /usr/lib/x86_64-linux-gnu/libc-2.31.so 
  10th: 000012CE(000012A0+0000002E) - _start - /disk/pro/github/error_catch/out
```
* 上图每行打印为: 跳转地址(函数地址+偏移量)、对应函数名、对应源文件的列表信息
* 行顺序显示程序最后阶段的函数跳转情况
* 虽然 ecapi_signal() 是最后一个函数, 但实际崩溃位置在 signal_6_abrt()
* signal_6_abrt() 出现异常后, 系统组织错误信号, 最后通过 killpg 函数发送到崩溃进程
* 崩溃进程收到信号, 按事先注册的信号回调, 跳转到 ecapi_signal() 函数
* ecapi_signal() 函数按前面"原理简介"的流程, 输出完上面的打印信息
