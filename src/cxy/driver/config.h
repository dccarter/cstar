//
// Created by Carter Mbotho on 2024-02-01.
//

#pragma once

#include <core/log.h>
#include <core/strpool.h>

typedef struct CompilerConfig {
    Log *L;
    MemPool *pool;
    StrPool *strings;
    void *ctx;
} CompilerConfig;
