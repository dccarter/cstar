//
// Created by Carter Mbotho on 2024-10-30.
//
#pragma once

#include <sys/stat.h>

typedef struct stat Stat;

int fs_stat(const char *path, struct stat *st);
int fs_lstat(const char *path, struct stat *st);
int fs_fstat(int fd, struct stat *st);
