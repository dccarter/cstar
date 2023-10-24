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
} CXY__scheduler = {NULL};

attr(always_inline) static tina *CXY__scheduler_pop()
{
    if (CXY__scheduler.pending.first == NULL)
        return NULL;

    tina *co = CXY__scheduler.pending.first;
    if (CXY__scheduler.pending.first == CXY__scheduler.pending.last) {
        CXY__scheduler.pending.first = CXY__scheduler.pending.last = NULL;
    }
    else {
        CXY__scheduler.pending.first = co->next;
    }

    return co;
}

attr(always_inline) static void CXY__scheduler_push(tina *co)
{
    if (CXY__scheduler.pending.last) {
        CXY__scheduler.pending.last->next = co;
        CXY__scheduler.pending.last = co;
    }
    else
        CXY__scheduler.pending.first = CXY__scheduler.pending.last = co;
}

attr(always_inline) static void CXY__cleanup_coroutine(tina *co)
{
    tina_swap(co, CXY__scheduler.cleanup_coro, co);
}

void CXY__scheduler_start(tina *main)
{
    CXY__scheduler_push(main);
    CXY__scheduler.main_coro = main;
    CXY__scheduler.running = true;
    tina_resume(CXY__scheduler.this_coro, NULL);
}

void CXY__scheduler_suspend(void)
{
    tina *co = tina_running();
    if (co != CXY__scheduler.this_coro) {
        tina_swap(tina_running(), CXY__scheduler.this_coro, NULL);
    }
}

void CXY__scheduler_schedule(tina *co)
{
    if (co != tina_running() && co != CXY__scheduler.this_coro) {
        CXY__scheduler_push(co);
    }
}

void CXY__scheduler_stop()
{
    CXY__scheduler.running = false;
    tina_swap(tina_running(), CXY__scheduler.this_coro, NULL);
}

void *CXY__scheduler_func(attr(unused) tina *this, void *arg)
{
    do {
        tina *co = CXY__scheduler_pop() ?: CXY__scheduler.loop_coro;
        tina_swap(this, co, NULL);
    } while (CXY__scheduler.running);

    CXY__cleanup_coroutine(this);

    unreachable("DONE!!!");
}

void *CXY__eventloop_coro(tina *co, void *arg)
{
    aeMain(CXY__scheduler.loop);
    aeDeleteEventLoop(CXY__scheduler.loop);
    return NULL;
}

void *CXY__cleanup_coro_handler(tina *co, void *arg)
{
    while (arg) {
        tina *done = arg;
        CXY__free(done->buffer);

        if (arg == CXY__scheduler.this_coro)
            break;

        arg = tina_swap(co, CXY__scheduler.this_coro, NULL);
    }

    tina_swap(co, CXY__scheduler.main_coro, NULL);

    unreachable("coroutine should never exit!!!");
}

void CXY__eventloop_init()
{
    CXY__scheduler.loop = aeCreateEventLoop(10);

    CXY__assert(CXY__scheduler.loop != NULL, "");
    CXY__scheduler.loop_coro =
        tina_init(NULL, CXY_DEFAULT_CORO_STACK_SIZE, CXY__eventloop_coro, NULL);
    CXY__scheduler.loop_coro->name = "mainLoopCoroutine";

    CXY__scheduler.cleanup_coro = tina_init(
        NULL, CXY_DEFAULT_CORO_STACK_SIZE, CXY__cleanup_coro_handler, NULL);
    CXY__scheduler.cleanup_coro->name = "cleanupCoroutine";

    CXY__scheduler.this_coro =
        tina_init(NULL, CXY_DEFAULT_CORO_STACK_SIZE, CXY__scheduler_func, NULL);
    CXY__scheduler.this_coro->name = "schedulingCoroutine";
}

void CXY__eventloop_callback(aeEventLoop *loop, int fd, void *arg, int mask)
{
    tina *co = arg;
    tina_swap(tina_running(), co, (void *)(intptr_t)mask);
}

int CXY__eventloop_timer_fired(struct aeEventLoop *loop, i64 id, void *arg)
{
    tina *co = arg;
    tina_swap(tina_running(), co, &id);
    return AE_NOMORE;
}

int CXY__eventloop_wait_read(int fd, int timeout)
{
    int status = aeCreateFileEvent(CXY__scheduler.loop,
                                   fd,
                                   AE_READABLE,
                                   CXY__eventloop_callback,
                                   tina_running());

    if (status == AE_OK) {
        void *result =
            tina_swap(tina_running(), CXY__scheduler.this_coro, NULL);
        return (int)(intptr_t)result;
    }

    return status;
}

int CXY__eventloop_wait_write(int fd, int timeout)
{
    int status = aeCreateFileEvent(CXY__scheduler.loop,
                                   fd,
                                   AE_WRITABLE,
                                   CXY__eventloop_callback,
                                   tina_running());

    if (status == AE_OK) {
        void *result =
            tina_swap(tina_running(), CXY__scheduler.this_coro, NULL);
        return (int)(intptr_t)result;
    }

    return status;
}

void CXY__eventloop_sleep(i64 ms)
{
    if (ms > 0) {
        int status = aeCreateTimeEvent(CXY__scheduler.loop,
                                       ms,
                                       CXY__eventloop_timer_fired,
                                       tina_running(),
                                       NULL);
        CXY__assert(status != AE_ERR, "");

        tina_swap(tina_running(), CXY__scheduler.this_coro, NULL);
    }
}

void *CXY__coro_fn(tina *co, void *arg)
{
    void (*fn)(void *) = co->user_data;
    fn(arg);

    CXY__cleanup_coroutine(co);
    unreachable("COROUTINE SHOULD HAVE EXITED");
}

void CXY__launch_coro(void (*fn)(void *), void *args, const char *dbg, u64 ss)
{
    tina *co = tina_init(NULL, ss, CXY__coro_fn, fn);
    co->name = dbg;

    CXY__scheduler_push(tina_running());
    tina_swap(tina_running(), co, args);
}
