
#if defined(CXY_MAIN_INVOKE) || defined(CXY_MAIN_INVOKE_RETURN)

int main(int argc, const char *argv[])
{
    int ret = 0;
#ifdef CXY_MAIN_INVOKE
    CXY__main((CXY__Main_Args_t){.data = argv, .count = argc});
#else
    int ret = CXY__main((CXY__Main_Args_t){.data = argv, .count = argc});
#endif

    return ret;
}
#endif
