#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The memory pool is a block-based allocator that allocates blocks of memory of
 * fixed dim to hold the allocated data. The blocks are reclaimed when the
 * memory pool is destroyed.
 */

struct MemBlock;
typedef enum { memAstNode, memType, memCOUNT } MemPoolCacheId;
typedef struct MemPoolCacheItem {
    struct MemPoolCacheItem *next;
    size_t size;
} MemPoolCacheItem;

typedef struct {
    MemPoolCacheItem *first;
    MemPoolCacheItem *last;
    size_t count;
    size_t allocs;
    size_t returns;
} MemPoolCache;

typedef struct TrackedMemoryItem {
    struct TrackedMemoryItem *next;
    struct TrackedMemoryItem *prev;
    size_t size;
} TrackedMemoryItem;

typedef struct MemPool {
    struct MemBlock *first;
    struct MemBlock *cur;
    MemPoolCache caches[memCOUNT];
    struct {
        TrackedMemoryItem *first;
        TrackedMemoryItem *last;
        size_t allocated;
    } trackedAllocations;
} MemPool;

typedef struct {
    size_t totalAllocated;
    size_t totalUsed;
    size_t numberOfBlocks;
} MemPoolStats;

MemPool newMemPool(void);
void *allocFromMemPool(MemPool *, size_t);
void *allocTrackedMem(MemPool *, size_t);
void *reallocTrackedMem(MemPool *, void *, size_t);
void *allocFromCacheOrPool(MemPool *, MemPoolCacheId, size_t);
void freeMemToPoolCache(MemPool *, MemPoolCacheId, void *, size_t);
void resetMemPool(MemPool *);
void freeMemPool(MemPool *);
void getMemPoolStats(const MemPool *, MemPoolStats *);

#ifdef __cplusplus
}
#endif
