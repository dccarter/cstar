
#if defined(CXY_MAIN_INVOKE) || defined(CXY_MAIN_INVOKE_RETURN)

int main(int argc, const char *argv[])
{
    tina main_coro = TINA_EMPTY;
    __cxy_main_coro = &main_coro;
    __running.co = __cxy_main_coro;
    __cxy_scheduler.running = true;

    __cxy_eventloop_init();

    int ret = 0;
#ifdef CXY_MAIN_INVOKE
    cxy_main(&(cxy_main_args_t){.data = argv, .len = argc});
#else
    int ret = cxy_main(&(cxy_main_args_t){.data = argv, .len = argc});
#endif

    __cxy_scheduler_stop();
    return ret;
}

#endif
