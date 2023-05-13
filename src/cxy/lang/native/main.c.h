
#ifdef CXY_MAIN_INVOKE

int main(int argc, const char *argv[])
{
#ifdef CXY_GC_ENABLED
    tgc_start(&__cxy_builtins_gc, &argc);
#endif

    CXY_MAIN_INVOKE((cxy_main_args_t){.data = argv, .len = argc});

#ifdef CXY_GC_ENABLED
    tgc_stop(&__cxy_builtins_gc);
#endif
}

#endif
