//
// Created by Carter Mbotho on 2023-07-13.
//
#ifndef __CXY_BUILD__
#include "async.h"
#endif

struct {
    struct {
        tina *first;
        tina *last;
    } pending;
    tina *loop_coro;
    tina *this_coro;
    tina *cleanup_coro;
    tina *main_coro;
    aeEventLoop *loop;
    bool running;
} __cxy_scheduler = {NULL};

attr(always_inline) static tina *__cxy_scheduler_pop()
{
    if (__cxy_scheduler.pending.first == NULL)
        return NULL;

    tina *co = __cxy_scheduler.pending.first;
    if (__cxy_scheduler.pending.first == __cxy_scheduler.pending.last) {
        __cxy_scheduler.pending.first = __cxy_scheduler.pending.last = NULL;
    }
    else {
        __cxy_scheduler.pending.first = co->next;
    }

    return co;
}

attr(always_inline) static void __cxy_scheduler_push(tina *co)
{
    if (__cxy_scheduler.pending.last) {
        __cxy_scheduler.pending.last->next = co;
        __cxy_scheduler.pending.last = co;
    }
    else
        __cxy_scheduler.pending.first = __cxy_scheduler.pending.last = co;
}

attr(always_inline) static void __cxy_cleanup_coroutine(tina *co)
{
    tina_swap(co, __cxy_scheduler.cleanup_coro, co);
}

void __cxy_scheduler_stop()
{
    __cxy_scheduler.running = false;
    tina_swap(tina_running(), __cxy_scheduler.this_coro, NULL);
}

void *__cxy_scheduler_func(attr(unused) tina *this, void *arg)
{
    do {
        tina *co = __cxy_scheduler_pop() ?: __cxy_scheduler.loop_coro;
        tina_swap(this, co, NULL);
    } while (__cxy_scheduler.running);

    __cxy_cleanup_coroutine(this);

    unreachable("DONE!!!");
}

void *__cxy_eventloop_coro(tina *co, void *arg)
{
    printf("Running event loop\n");
    aeMain(__cxy_scheduler.loop);
    aeDeleteEventLoop(__cxy_scheduler.loop);
    printf("Done running event loop\n");

    return NULL;
}

void *__cxy_cleanup_coro_handler(tina *co, void *arg)
{
    while (arg) {
        tina *done = arg;
        __cxy_free(done->buffer);

        if (arg == __cxy_scheduler.this_coro)
            break;

        arg = tina_swap(co, __cxy_scheduler.this_coro, NULL);
    }

    tina_swap(co, __cxy_scheduler.main_coro, NULL);

    unreachable("coroutine should never exit!!!");
}

void __cxy_eventloop_init()
{
    __cxy_scheduler.loop = aeCreateEventLoop(10);

    __cxy_assert(__cxy_scheduler.loop != NULL, "");
    __cxy_scheduler.loop_coro = tina_init(
        NULL, CXY_DEFAULT_CORO_STACK_SIZE, __cxy_eventloop_coro, NULL);
    __cxy_scheduler.loop_coro->name = "mainLoopCoroutine";

    __cxy_scheduler.cleanup_coro = tina_init(
        NULL, CXY_DEFAULT_CORO_STACK_SIZE, __cxy_cleanup_coro_handler, NULL);
    __cxy_scheduler.cleanup_coro->name = "cleanupCoroutine";

    __cxy_scheduler.this_coro = tina_init(
        NULL, CXY_DEFAULT_CORO_STACK_SIZE, __cxy_scheduler_func, NULL);
    __cxy_scheduler.this_coro->name = "schedulingCoroutine";
}

void __cxy_eventloop_callback(aeEventLoop *loop, int fd, void *arg, int mask)
{
    tina *co = arg;
    tina_swap(tina_running(), co, (void *)(intptr_t)mask);
}

int __cxy_eventloop_timer_fired(struct aeEventLoop *loop, i64 id, void *arg)
{
    tina *co = arg;
    tina_swap(tina_running(), co, &id);
    return AE_NOMORE;
}

int __cxy_eventloop_wait_read(int fd, int timeout)
{
    int status = aeCreateFileEvent(__cxy_scheduler.loop,
                                   fd,
                                   AE_READABLE,
                                   __cxy_eventloop_callback,
                                   tina_running());

    if (status == AE_OK) {
        void *result =
            tina_swap(tina_running(), __cxy_scheduler.this_coro, NULL);
        return (int)(intptr_t)result;
    }

    return status;
}

int __cxy_eventloop_wait_write(int fd, int timeout)
{
    int status = aeCreateFileEvent(__cxy_scheduler.loop,
                                   fd,
                                   AE_WRITABLE,
                                   __cxy_eventloop_callback,
                                   tina_running());

    if (status == AE_OK) {
        void *result =
            tina_swap(tina_running(), __cxy_scheduler.this_coro, NULL);
        return (int)(intptr_t)result;
    }

    return status;
}

void __cxy_eventloop_sleep(i64 ms)
{
    if (ms > 0) {
        int status = aeCreateTimeEvent(__cxy_scheduler.loop,
                                       ms,
                                       __cxy_eventloop_timer_fired,
                                       tina_running(),
                                       NULL);
        __cxy_assert(status != AE_ERR, "");

        tina_swap(tina_running(), __cxy_scheduler.this_coro, NULL);
    }
}

void *__cxy_coro_fn(tina *co, void *arg)
{
    void (*fn)(void *) = co->user_data;
    fn(arg);

    __cxy_cleanup_coroutine(co);
    unreachable("COROUTINE SHOULD HAVE EXITED");
}

void __cxy_launch_coro(void (*fn)(void *), void *args, const char *dbg, u64 ss)
{
    tina *co = tina_init(NULL, ss, __cxy_coro_fn, fn);
    co->name = dbg;

    __cxy_scheduler_push(tina_running());
    tina_swap(tina_running(), co, args);
}
