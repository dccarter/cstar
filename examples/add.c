#include "add.h"
#include "unistd.h"
int fs_stat(const char *path, struct stat *st)
{
    S_IFREG
    return stat(path, st);
}