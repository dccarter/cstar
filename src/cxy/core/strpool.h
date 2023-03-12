
#pragma once

#include "core/htable.h"

/*
 * The string pool is a hashed container for strings. Each string contained in
 * the pool is stored uniquely, which allows fast comparison (strings can then
 * be compared by their address).
 */

typedef struct MemPool MemPool;

typedef struct {
    MemPool *mem_pool;
    HashTable hash_table;
} StrPool;

StrPool newStrPool(MemPool *);
void freeStrPool(StrPool *);

const char *makeString(StrPool *, const char *);


