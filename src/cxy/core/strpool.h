
#pragma once

#include "core/htable.h"

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
const char *makeStringSized(StrPool *, const char *, u64);
const char *makeAnonymousVariable(StrPool *pool, const char *prefix);
const char *makeStringConcat(StrPool *, const char *, ...);
