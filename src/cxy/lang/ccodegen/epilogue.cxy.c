typedef const char **__cxy_main_argv_t;

int main(int argc, const char *argv[])
{
    __cxy_main_argv_t args = argv;
    __CXY_MAIN_INVOKE(args);
}
