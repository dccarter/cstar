
#ifdef CXY_MAIN_INVOKE

int main(int argc, const char *argv[])
{
    CXY_MAIN_INVOKE((cxy_main_args_t){.data = argv, .len = argc});
}

#endif
