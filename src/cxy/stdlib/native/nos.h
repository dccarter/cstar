//
// Created by Carter Mbotho on 2024-10-30.
//
#pragma once

struct stat;

int fs_stat(const char *path, struct stat *st);
int fs_fstat(int fd, struct stat *st);
