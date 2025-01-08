//
// Created by Carter Mbotho on 2024-12-10.
//

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/hash.h"
#include "core/utils.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef HashCode (*HmapHashFunc)(const void *data);
typedef bool (*HmapEqualFunc)(const void *lhs, const void *rhs);

typedef struct Hmap {
    void *data;
    u64 elemSize;
    u64 nslots;
    u64 nitems;
    HmapHashFunc hash;
    HmapEqualFunc equal;
    void *sspace0;
    void *sspace1;
} Hmap;

typedef struct HmapIterator {
    Hmap *hmap;
    u64 index;
} HmapIterator;

typedef CxyPair(void *, bool) HmapStatus;

void hmapInitWithCapacity(Hmap *self,
                          u64 elemSize,
                          HmapHashFunc hash,
                          HmapEqualFunc equal,
                          u64 capacity);
void hmapDestroy(Hmap *self);
void hmapClear(Hmap *self);
HmapStatus hmapPut(Hmap *self, void *data);
void hmapRemove(Hmap *self, void *data);
void *hmapGet(Hmap *self, void *data);

HmapIterator hmapIterator(Hmap *self);
void *hmapNext(HmapIterator *iterator);

static inline Hmap hmapCreateWithCapacity(u64 elemSize,
                                          HmapHashFunc hash,
                                          HmapEqualFunc equal,
                                          u64 capacity)
{
    Hmap self = {};
    hmapInitWithCapacity(&self, elemSize, hash, equal, capacity);
    return self;
}

static inline Hmap hmapCreate(u64 elemSize,
                              HmapHashFunc hash,
                              HmapEqualFunc equal)
{
    return hmapCreateWithCapacity(elemSize, hash, equal, 0);
}

static inline void hmapInit(Hmap *self,
                            u64 elemSize,
                            HmapHashFunc hash,
                            HmapEqualFunc equal)
{
    return hmapInitWithCapacity(self, elemSize, hash, equal, 0);
}

#ifdef __cplusplus
}
#endif
