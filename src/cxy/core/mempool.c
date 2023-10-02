#include "core/mempool.h"
#include "core/alloc.h"

#include <assert.h>
#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

typedef struct MemBlock {
    size_t size;
    size_t capacity;
    struct MemBlock *next;
    alignas(max_align_t) char data[];
} MemBlock;

#define MIN_MEM_BLOCK_CAPACITY 1024

MemPool newMemPool(void)
{
    MemPool pool = {NULL, NULL};
    memset(&pool, 0, sizeof(pool));
    return pool;
}

static size_t remainingMem(MemBlock *block)
{
    assert(block->capacity >= block->size);
    return block->capacity - block->size;
}

static MemBlock *allocMemBlock(MemBlock *prev, size_t capacity)
{
    if (capacity < MIN_MEM_BLOCK_CAPACITY)
        capacity = MIN_MEM_BLOCK_CAPACITY;
    MemBlock *block = mallocOrDie(sizeof(MemBlock) + capacity);
    block->capacity = capacity;
    block->size = 0;
    block->next = NULL;
    if (prev)
        prev->next = block;
    return block;
}

static size_t alignTo(size_t size, size_t align)
{
    size_t offset = size % align;
    return offset != 0 ? size + align - offset : size;
}

void *allocFromMemPool(MemPool *mem_pool, size_t size)
{
    if (size == 0)
        return NULL;
    size = alignTo(size, sizeof(max_align_t));
    if (!mem_pool->cur) {
        mem_pool->first = mem_pool->cur = allocMemBlock(NULL, size);
    }
    else {
        // Try to re-use the next memory pools if they are appropriately sized
        while (remainingMem(mem_pool->cur) < size) {
            if (!mem_pool->cur->next) {
                mem_pool->cur = allocMemBlock(mem_pool->cur, size);
                break;
            }
            mem_pool->cur = mem_pool->cur->next;
            assert(mem_pool->cur->size == 0 &&
                   "next memory pool block must have been reset");
        }
    }
    assert(remainingMem(mem_pool->cur) >= size);
    void *ptr = ((char *)mem_pool->cur->data) + mem_pool->cur->size;
    mem_pool->cur->size += size;
    return ptr;
}

void *allocTrackedMem(MemPool *pool, size_t size)
{
    TrackedMemoryItem *item = mallocOrDie(size + sizeof(TrackedMemoryItem));
    item->next = item->prev = NULL;
    item->size = size;
    if (pool->trackedAllocations.first == NULL) {
        pool->trackedAllocations.first = item;
    }
    else {
        item->prev = pool->trackedAllocations.last;
        pool->trackedAllocations.last->next = item;
    }
    pool->trackedAllocations.last = item;
    pool->trackedAllocations.allocated += size;
    return item + 1;
}

void *reallocTrackedMem(MemPool *pool, void *ptr, size_t size)
{
    TrackedMemoryItem *item = ((TrackedMemoryItem *)ptr) - 1;
    pool->trackedAllocations.allocated -= item->size;
    item = reallocOrDie(item, size);
    item->size = size;
    pool->trackedAllocations.allocated += size;

    if (item->prev)
        item->prev->next = item;
    return item + 1;
}

void *allocFromCacheOrPool(MemPool *pool, MemPoolCacheId cache, size_t size)
{
    MemPoolCache *poolCache = &pool->caches[cache];
    poolCache->allocs++;
    if (poolCache->first == NULL)
        return allocFromMemPool(pool, size);
    void *ptr = poolCache->first;
    poolCache->first = poolCache->first->next;
    poolCache->count--;
    return ptr;
}

void freeMemToPoolCache(MemPool *pool,
                        MemPoolCacheId cache,
                        void *ptr,
                        size_t size)
{
    MemPoolCache *poolCache = &pool->caches[cache];
    poolCache->returns++;
    MemPoolCacheItem *item = ptr;
    memset(ptr, 0, size);
    item->size = size;
    if (poolCache->first == NULL) {
        poolCache->first = item;
    }
    else {
        poolCache->last->next = item;
    }
    poolCache->last = item;
    poolCache->count++;
}

void resetMemPool(MemPool *mem_pool)
{
    MemBlock *block = mem_pool->first;
    while (block) {
        block->size = 0;
        block = block->next;
    }
    mem_pool->cur = mem_pool->first;
}

void freeMemPool(MemPool *mem_pool)
{
    MemBlock *block = mem_pool->first;
    while (block) {
        MemBlock *next = block->next;
        free(block);
        block = next;
    }
    mem_pool->first = mem_pool->cur = NULL;
    memset(mem_pool->caches, 0, sizeof(mem_pool->caches));
    TrackedMemoryItem *item = mem_pool->trackedAllocations.first;
    for (; item;) {
        TrackedMemoryItem *cur = item;
        item = item->next;
        free(cur);
    }
    mem_pool->trackedAllocations.first = mem_pool->trackedAllocations.last =
        NULL;
    mem_pool->trackedAllocations.allocated = 0;
}

void getMemPoolStats(const MemPool *pool, MemPoolStats *stats)
{
    *stats = (MemPoolStats){};
    const MemBlock *block = pool->first;
    while (block) {
        stats->numberOfBlocks++;
        stats->totalAllocated += block->capacity;
        stats->totalUsed += block->size;
        block = block->next;
    }

    stats->totalUsed += pool->trackedAllocations.allocated;
}
