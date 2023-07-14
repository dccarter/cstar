//
// Created by Carter Mbotho on 2023-07-13.
//
#pragma once
#ifndef __CXY_BUILD__
#include "epilogue.h"

#include "coro/tina.h"
#include "evloop/ae.h"
#endif

#define _TINYCTHREAD_ASSERT __cxy_assert
#define _TINYCTHREAD_ALLOC(size) __cxy_alloc(size, nullptr)
#define _TINYCTHREAD_FREE __cxy_free

#define _TINA_ASSERT __cxy_assert
#define _TINA_ALLOC(size) __cxy_alloc(size, nullptr)
#define _TINA_FREE __cxy_free

#ifndef CXY_DEFAULT_CORO_STACK_SIZE
#define CXY_DEFAULT_CORO_STACK_SIZE 32 * 1024
#endif

#ifndef CXY_DEFAULT_CORO_STACK_SIZE
#define CXY_DEFAULT_CORO_STACK_SIZE 256 * 1024
#endif

#ifndef CXY_MAX_EVENT_LOOP_FDS
#define CXY_MAX_EVENT_LOOP_FDS 1024
#endif

void __cxy_eventloop_init();
int __cxy_eventloop_wait_read(int fd, int timeout);
int __cxy_eventloop_wait_write(int fd, int timeout);
void __cxy_eventloop_sleep(i64 ms);
void __cxy_launch_coro(void (*fn)(void *), void *args, const char *dbg, u64 ss);
void __cxy_scheduler_stop();

attr(always_inline) static const char *__cxy_coroutine_name(tina *co)
{
    return (co ?: tina_running())->name ?: "<unnamed>";
}

attr(always_inline) i64 __cxy_now_ms()
{
    long seconds = 0, ms = 0;
    aeGetTime(&seconds, &ms);
    return (seconds * 1000) + ms;
}
