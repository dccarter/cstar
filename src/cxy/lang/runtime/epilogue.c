
#if defined(CXY_MAIN_INVOKE) || defined(CXY_MAIN_INVOKE_RETURN)

int ret = 0;

void *CXY__main_coro(tina *co, void *arg)
{
    CXY__Main_Args_t *args = co->user_data;

#ifdef CXY_MAIN_INVOKE
    CXY__main(*args);
#else
    ret = CXY__main(*args);
#endif
    CXY__scheduler_stop();
    exit(ret);
}

int main(int argc, const char *argv[])
{
    CXY__eventloop_init();
    CXY__Main_Args_t args = {.data = argv, .count = argc};

    tina *co = tina_init(
        NULL, CXY_MAIN_CORO_STACK_SIZE, CXY__main_coro, (void *)&args);
    co->name = "CXY__main_coro";

    CXY__scheduler_start(co);
    return 0;
}
#endif
