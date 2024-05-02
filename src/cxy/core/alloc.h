
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "core/utils.h"

static inline void *mallocOrDie(size_t size)
{
    void *ptr = malloc(size);
    csAssert(ptr, "out of memory, malloc() failed\n"); // GCOV_EXCL_LINE
    return ptr;
}

static inline void *callocOrDie(size_t count, size_t size)
{
    void *ptr = calloc(count, size);
    csAssert(ptr, "out of memory, calloc() failed\n"); // GCOV_EXCL_LINE
    return ptr;
}

static inline void *reallocOrDie(void *ptr, size_t size)
{
    ptr = realloc(ptr, size);
    csAssert(ptr, "out of memory, realloc() failed\n"); // GCOV_EXCL_LINE
    return ptr;
}

#ifdef __cplusplus
}
#endif
