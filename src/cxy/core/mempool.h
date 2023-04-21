#pragma once

#include <stddef.h>

/*
 * The memory pool is a block-based allocator that allocates blocks of memory of
 * fixed dim to hold the allocated data. The blocks are reclaimed when the
 * memory pool is destroyed.
 */

struct MemBlock;

typedef struct MemPool {
    struct MemBlock *first;
    struct MemBlock *cur;
} MemPool;

typedef struct {
    size_t totalAllocated;
    size_t totalUsed;
    size_t numberOfBlocks;
} MemPoolStats;

MemPool newMemPool(void);
void *allocFromMemPool(MemPool *, size_t);
void resetMemPool(MemPool *);
void freeMemPool(MemPool *);
void getMemPoolStats(const MemPool *, MemPoolStats *);
