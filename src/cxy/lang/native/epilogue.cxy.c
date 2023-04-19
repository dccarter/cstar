typedef const char **cxy_main_argv_t;

int main(int argc, const char *argv[])
{
    cxy_main_argv_t args = argv;
    CXY_MAIN_INVOKE(args);
}
