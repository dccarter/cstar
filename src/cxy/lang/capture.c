//
// Created by Carter on 2023-03-31.
//

#include "capture.h"
#include <string.h>

typedef struct {
    cstring name;
    const Type *type;
    u64 id;
} Capture;

static bool compareCaptures(const void *left, const void *right)
{
    return strcmp((*(const Capture **)left)->name,
                  (*(const Capture **)right)->name) == 0;
}

u64 addClosureCapture(ClosureCapture *set, cstring name, const Type *type)
{
    HashCode hash = hashStr(hashInit(), name);
    if (set->index == 0) {
        // Lazy initialization
        set->table = newHashTable(sizeof(Capture));
    }
    Capture cap = {.name = name, .type = type, .id = set->index};

    const Capture *found = findInHashTable(&set->table, //
                                           &cap,
                                           hash,
                                           sizeof(Capture *),
                                           compareCaptures);
    if (found) {
        csAssert0(found->type == type);
        return found->id;
    }

    if (!insertInHashTable(
            &set->table, &cap, hash, sizeof(Capture *), compareCaptures))
        csAssert0("failing to insert in type table");

    return set->index++;
}
typedef struct {
    const Type **arr;
    u64 count;
} OrderedCaptureCtx;

bool populateOrderedCapture(void *ctx, const void *elem)
{
    OrderedCaptureCtx *dst = ctx;
    const Capture *cap = elem;
    if (cap->id < dst->count)
        dst->arr[cap->id] = cap->type;
    return true;
}

u64 getOrderedCapture(ClosureCapture *set, const Type **capture, u64 count)
{
    OrderedCaptureCtx ctx = {.arr = capture, .count = MIN(set->index, count)};
    enumerateHashTable(
        &set->table, &ctx, populateOrderedCapture, sizeof(Capture));
    return ctx.count;
}
