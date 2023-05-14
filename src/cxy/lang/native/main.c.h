
#if defined(CXY_MAIN_INVOKE) || defined(CXY_MAIN_INVOKE_RETURN)

int main(int argc, const char *argv[])
{
#ifdef CXY_GC_ENABLED
    tgc_start(&__cxy_builtins_gc, &argc);
#endif

#ifdef CXY_MAIN_INVOKE
    cxy_main((cxy_main_args_t){.data = argv, .len = argc});
    tgc_stop(&__cxy_builtins_gc);
    return 0;
#else
    int ret = cxy_main((cxy_main_args_t){.data = argv, .len = argc});
    tgc_stop(&__cxy_builtins_gc);
    return ret;
#endif
}

#endif
