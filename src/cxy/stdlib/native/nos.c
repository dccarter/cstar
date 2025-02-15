//
// Created by Carter Mbotho on 2024-10-30.
//

#include "nos.h"
#include <sys/stat.h>

int fs_stat(const char *path, struct stat *st) { return stat(path, st); }
int fs_lstat(const char *path, struct stat *st) { return lstat(path, st); }
int fs_fstat(int fd, struct stat *st) { return fstat(fd, st); }

Dirent *nos_readdir(DIR *dir) {
    return (Dirent *)readdir(dir);
}
