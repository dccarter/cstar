//
// Created by Carter Mbotho on 2023-07-13.
//
#pragma once
#ifndef __CXY_BUILD__
#include "epilogue.h"

#include "coro/tina.h"
#include "evloop/ae.h"
#endif

#define _TINYCTHREAD_ASSERT CXY__assert
#define _TINYCTHREAD_ALLOC(size) CXY__alloc(size, nullptr)
#define _TINYCTHREAD_FREE CXY__free

#define _TINA_ASSERT CXY__assert
#define _TINA_ALLOC(size) CXY__alloc(size, nullptr)
#define _TINA_FREE CXY__free

#ifndef CXY_DEFAULT_CORO_STACK_SIZE
#define CXY_DEFAULT_CORO_STACK_SIZE 256 * 1024
#endif

#ifndef CXY_MAIN_CORO_STACK_SIZE
#define CXY_MAIN_CORO_STACK_SIZE 256 * 1024
#endif

#ifndef CXY_MAX_EVENT_LOOP_FDS
#define CXY_MAX_EVENT_LOOP_FDS 1024
#endif

void CXY__eventloop_init();
int CXY__eventloop_wait_read(int fd, int timeout);
int CXY__eventloop_wait_write(int fd, int timeout);
void CXY__eventloop_poke(void);
void CXY__eventloop_sleep(i64 ms);
void CXY__launch_coro(void (*fn)(void *), void *args, const char *dbg, u64 ss);
void CXY__scheduler_start(tina *main);
void CXY__scheduler_suspend(void);
void CXY__scheduler_schedule(tina *co);
void CXY__scheduler_stop();

attr(always_inline) static const char *CXY__coroutine_name(tina *co)
{
    return (co ?: tina_running())->name ?: "<unnamed>";
}

attr(always_inline) i64 CXY__now_ms()
{
    long seconds = 0, ms = 0;
    aeGetTime(&seconds, &ms);
    return (seconds * 1000) + ms;
}
