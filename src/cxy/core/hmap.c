//
// Created by Carter Mbotho on 2024-12-11.
//

#include "hmap.h"

#include <stdlib.h>
#include <string.h>

enum { HMAP_PRIMES_COUNT = 24 };

static const u64 HMAP_RIMES[HMAP_PRIMES_COUNT] = {
    0,     1,      5,      11,     23,      53,      101,     197,
    389,   683,    1259,   2417,   4733,    9371,    18617,   37097,
    74093, 148073, 296099, 592019, 1100009, 2200013, 4400021, 8800019};

#define HMAP_LOAD_FACTOR 0.9

static u64 hmapIdealSize(u64 size)
{
    size = (size_t)((double)(size + 1) / HMAP_LOAD_FACTOR);
    for (u64 i = 0; i < HMAP_PRIMES_COUNT; i++) {
        if (HMAP_RIMES[i] >= size) {
            return HMAP_RIMES[i];
        }
    }
    u64 last = HMAP_RIMES[HMAP_PRIMES_COUNT - 1];
    while (last < size)
        last <<= 1;
    return last;
}

static inline u64 hmapStep(const Hmap *self)
{
    return sizeof(u64) + self->elemSize;
}

static u64 hmapItemHash(const Hmap *self, u64 i)
{
    return *(u64 *)((char *)self->data + i * hmapStep(self));
}

static void *hmapItem(const Hmap *self, u64 i)
{
    return (char *)self->data + i * hmapStep(self) + sizeof(u64);
}

static i64 hmapProbe(const Hmap *self, u64 i, u64 h)
{
    i64 v = i - (h - 1);
    if (v < 0) {
        v = self->nslots + v;
    }
    return v;
}

static u64 hmapSizeRound(u64 s)
{
    return ((s + sizeof(void *) - 1) / sizeof(void *)) * sizeof(void *);
}

static void *hmapSwapSpaceItem(Hmap *self, void *space)
{
    return (char *)space + sizeof(u64);
}

static void hmapRehash(Hmap *self, u64 newCapacity)
{

    void *oldData = self->data;
    u64 oldCapacity = self->nslots;

    self->nslots = newCapacity;
    self->nitems = 0;
    self->data = calloc(self->nslots, hmapStep(self));

    for (size_t i = 0; i < oldCapacity; i++) {
        u64 h = *(u64 *)((char *)oldData + i * hmapStep(self));
        if (h != 0) {
            void *data = (char *)oldData + i * hmapStep(self) + sizeof(u64);
            hmapPut(self, data);
        }
    }

    free(oldData);
}

static void hmapResizeMore(Hmap *self)
{
    u64 newSize = hmapIdealSize(self->nitems);
    u64 oldCapacity = self->nslots;
    if (newSize > oldCapacity) {
        hmapRehash(self, newSize);
    }
}

static void hmapResizeLess(Hmap *self)
{
    u64 newSize = hmapIdealSize(self->nitems);
    u64 oldSize = self->nslots;
    if (newSize < oldSize) {
        hmapRehash(self, newSize);
    }
}

static HmapStatus hmapPutInternal(Hmap *self, void *data)
{
    u64 i = self->hash(data) % self->nslots;
    u64 j = 0;

    memset(self->sspace0, 0, hmapStep(self));
    memset(self->sspace1, 0, hmapStep(self));

    u64 ih = i + 1;
    memcpy((char *)self->sspace0, &ih, sizeof(u64));
    memcpy((char *)self->sspace0 + sizeof(u64), data, self->elemSize);

    while (true) {

        u64 h = hmapItemHash(self, i);
        if (h == 0) {
            self->nitems++;
            data = memcpy((char *)self->data + i * hmapStep(self),
                          self->sspace0,
                          hmapStep(self));
            return (HmapStatus){data, true};
        }

        if (self->equal(hmapItem(self, i),
                        hmapSwapSpaceItem(self, self->sspace0))) {
            memcpy((char *)self->data + i * hmapStep(self),
                   self->sspace0,
                   hmapStep(self));
            return (HmapStatus){(char *)self->data + i * hmapStep(self), false};
        }

        u64 p = hmapProbe(self, i, h);
        if (j >= p) {
            memcpy((char *)self->sspace1,
                   (char *)self->data + i * hmapStep(self),
                   hmapStep(self));
            memcpy((char *)self->data + i * hmapStep(self),
                   (char *)self->sspace0,
                   hmapStep(self));
            memcpy(
                (char *)self->sspace0, (char *)self->sspace1, hmapStep(self));
            j = p;
        }

        i = (i + 1) % self->nslots;
        j++;
    }
}

void hmapInitWithCapacity(Hmap *self,
                          u64 elemSize,
                          HmapHashFunc hash,
                          HmapEqualFunc equal,
                          u64 capacity)
{
    self->elemSize = elemSize;
    self->hash = hash;
    self->equal = equal;
    self->nslots = hmapIdealSize(capacity);

    if (self->nslots > 0) {
        self->data = calloc(self->nslots, hmapStep(self));
        self->sspace0 = calloc(1, hmapStep(self));
        self->sspace1 = calloc(1, hmapStep(self));
    }
    else {
        self->data = NULL;
    }
}

void hmapDestroy(Hmap *self)
{
    if (self->data != NULL) {
        free(self->data);
        free(self->sspace0);
        free(self->sspace1);
        self->data = NULL;
    }
}

void hmapClear(Hmap *self)
{
    free(self->data);
    self->nslots = 0;
    self->nitems = 0;
    self->data = NULL;
}

void hmapRemove(Hmap *self, void *data)
{
    if (self->nslots == 0) {
        return;
    }

    u64 i = self->hash(data) % self->nslots;
    u64 j = 0;

    while (true) {

        u64 h = hmapItemHash(self, i);
        if (h == 0 || j > hmapProbe(self, i, h)) {
            return;
        }

        if (self->equal(hmapItem(self, i), data)) {
            memset((char *)self->data + i * hmapStep(self), 0, hmapStep(self));

            while (true) {
                u64 ni = (i + 1) % self->nslots;
                u64 nh = hmapItemHash(self, ni);
                if (nh != 0 && hmapProbe(self, ni, nh) > 0) {
                    memcpy((char *)self->data + i * hmapStep(self),
                           (char *)self->data + ni * hmapStep(self),
                           hmapStep(self));
                    memset((char *)self->data + ni * hmapStep(self),
                           0,
                           hmapStep(self));
                    i = ni;
                }
                else {
                    break;
                }
            }

            self->nitems--;
            hmapResizeLess(self);
            return;
        }

        i = (i + 1) % self->nslots;
        j++;
    }
}

HmapStatus hmapPut(Hmap *self, void *data)
{
    HmapStatus status = hmapPutInternal(self, data);
    if (status.s)
        hmapResizeMore(self);
    return status;
}

void *hmapGet(Hmap *self, void *data)
{
    if (data >= self->data &&
        (char *)data < (char *)self->data + self->nslots * hmapStep(self)) {
        return hmapItem(self,
                        ((char *)data - (char *)self->data) / hmapStep(self));
    }

    if (self->nslots == 0) {
        return NULL;
    }

    u64 i = self->hash(data) % self->nslots;
    u64 j = 0;

    while (true) {
        u64 h = hmapItemHash(self, i);
        if (h == 0 || j > hmapProbe(self, i, h))
            return NULL;

        if (self->equal(hmapItem(self, i), data)) {
            return hmapItem(self, i);
        }

        i = (i + 1) % self->nslots;
        j++;
    }
}

HmapIterator hmapIterator(Hmap *self) { return (HmapIterator){self, 0}; }

void *hmapNext(HmapIterator *iterator)
{
    Hmap *self = iterator->hmap;
    while (iterator->index < self->nslots) {
        u64 i = iterator->index++;
        u64 h = hmapItemHash(self, i);
        if (h != 0)
            return hmapItem(self, i);
    }
    return NULL;
}
