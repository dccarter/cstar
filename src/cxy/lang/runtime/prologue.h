
#if defined(CXY_MAIN_INVOKE) || defined(CXY_MAIN_INVOKE_RETURN)

int main(int argc, const char *argv[])
{
    int ret = 0;
#ifdef CXY_MAIN_INVOKE
    cxy_main(&(cxy_main_args_t){.data = argv, .len = argc});
#else
    int ret = cxy_main(&(cxy_main_args_t){.data = argv, .len = argc});
#endif

    return ret;
}

#endif
