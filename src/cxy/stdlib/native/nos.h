//
// Created by Carter Mbotho on 2024-10-30.
//
#pragma once

#include <sys/stat.h>
#include <dirent.h>

typedef struct stat Stat;

int fs_stat(const char *path, struct stat *st);
int fs_lstat(const char *path, struct stat *st);
int fs_fstat(int fd, struct stat *st);

#if __APPLE__
typedef struct _Dirent __DARWIN_STRUCT_DIRENTRY __attribute__((__packed__)) Dirent;
#else
typedef struct dirent Dirent;
#endif

Dirent *nos_readdir(DIR *dir);
