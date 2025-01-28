
#pragma once

#include "core/htable.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The string pool is a hashed container for strings. Each string contained in
 * the pool is stored uniquely, which allows fast comparison (strings can then
 * be compared by their address).
 */

typedef struct MemPool MemPool;

#ifndef MAX_ANONYMOUS_PREFIX_SIZE
#define MAX_ANONYMOUS_PREFIX_SIZE 256
#endif

typedef struct StrPool {
    MemPool *mem_pool;
    HashTable hash_table;
} StrPool;

StrPool newStrPool(MemPool *);

void freeStrPool(StrPool *);

const char *makeString(StrPool *, const char *);

const char *makeTrimmedString(StrPool *, const char *);

const char *makeStringSized(StrPool *, const char *, u64);

const char *makeAnonymousVariable(StrPool *pool, const char *prefix);

const char *makeStringf(StrPool *strings, const char *fmt, ...);
const char *makeStringConcat_(StrPool *, const char *, ...);

cstring joinPath_(StrPool *pool, const char *p1, ...);

#define joinPath(P, S1, ...) joinPath_((P), (S1), ##__VA_ARGS__, NULL)

#define makeStringConcat(P, S1, ...)                                           \
    makeStringConcat_((P), (S1), ##__VA_ARGS__, NULL)

const char *makeStringf(StrPool *strings, const char *fmt, ...);

#ifdef __cplusplus
}
#endif