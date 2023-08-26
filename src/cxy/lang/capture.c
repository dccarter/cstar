//
// Created by Carter on 2023-03-31.
//

#include "capture.h"

#include <core/alloc.h>

#include <string.h>

typedef struct {
    cstring name;
    const AstNode *node;
    u64 id;
} Capture;

static bool compareCaptures(const void *left, const void *right)
{
    return (((const Capture *)left)->name == ((const Capture *)right)->name) ||
           strcmp(((const Capture *)left)->name,
                  ((const Capture *)right)->name) == 0;
}

static void initClosureCapture(ClosureCapture *set)
{
    set->table = mallocOrDie(sizeof(HashTable));
    *set->table = newHashTable(sizeof(Capture));
}

u64 addClosureCapture(ClosureCapture *set, cstring name, const AstNode *node)
{
    HashCode hash = hashStr(hashInit(), name);
    Capture cap = {.name = name, .node = node, .id = set->index};

    if (set->table == NULL)
        initClosureCapture(set);

    const Capture *found = findInHashTable(set->table, //
                                           &cap,
                                           hash,
                                           sizeof(Capture),
                                           compareCaptures);
    if (found) {
        csAssert0(found->node == node);
        return found->id;
    }

    if (!insertInHashTable(
            set->table, &cap, hash, sizeof(Capture), compareCaptures))
        csAssert0("failing to insert in type table");

    return set->index++;
}

typedef struct {
    const AstNode **captures;
    u64 count;
} OrderedCaptureCtx;

static bool populateOrderedCapture(void *ctx, const void *elem)
{
    OrderedCaptureCtx *dst = ctx;
    const Capture *cap = elem;
    if (cap->id < dst->count) {
        dst->captures[cap->id] = cap->node;
    }
    return true;
}

u64 getOrderedCapture(ClosureCapture *set, const AstNode **capture, u64 count)
{
    if (set->table) {
        OrderedCaptureCtx ctx = {.captures = capture,
                                 .count = MIN(set->index, count)};
        enumerateHashTable(
            set->table, &ctx, populateOrderedCapture, sizeof(Capture));
        return ctx.count;
    }

    return 0;
}

cstring getCapturedNodeName(const AstNode *node)
{
    switch (node->tag) {
    case astVarDecl:
        return node->varDecl.name;
    case astFuncParam:
        return node->funcParam.name;
    case astStructField:
        return node->structField.name;
    default:
        unreachable("Only variables and function params can be captured");
    }
}
