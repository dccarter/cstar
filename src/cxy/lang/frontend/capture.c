//
// Created by Carter on 2023-03-31.
//

#include "capture.h"

#include "core/alloc.h"

#include <string.h>

static bool compareCaptures(const void *left, const void *right)
{
    cstring lhs = getCapturedNodeName(((const Capture *)left)->node),
            rhs = getCapturedNodeName(((const Capture *)right)->node);
    return (lhs == rhs) || strcmp(lhs, rhs) == 0;
}

static void initClosureCapture(ClosureCapture *set)
{
    set->table = mallocOrDie(sizeof(HashTable));
    *set->table = newHashTable(sizeof(Capture));
}

Capture *addClosureCapture(ClosureCapture *set, AstNode *node)
{
    HashCode hash = hashStr(hashInit(), getCapturedNodeName(node));
    Capture cap = {.node = node, .id = set->index};

    if (set->table == NULL)
        initClosureCapture(set);

    if (insertInHashTable(
            set->table, &cap, hash, sizeof(Capture), compareCaptures))
        set->index++;

    Capture *found = findInHashTable(set->table, //
                                     &cap,
                                     hash,
                                     sizeof(Capture),
                                     compareCaptures);
    if (found) {
        csAssert0(found->node == node);
        return found;
    }

    return NULL;
}

typedef struct {
    Capture *captures;
    u64 count;
} OrderedCaptureCtx;

static bool populateOrderedCapture(void *ctx, const void *elem)
{
    OrderedCaptureCtx *dst = ctx;
    const Capture *cap = elem;
    if (cap->id < dst->count) {
        dst->captures[cap->id] = *cap;
    }
    return true;
}

u64 getOrderedCapture(ClosureCapture *set, Capture *capture, u64 count)
{
    if (set->table) {
        OrderedCaptureCtx ctx = {.captures = capture,
                                 .count = MIN(set->index, count)};
        enumerateHashTable(
            set->table, &ctx, populateOrderedCapture, sizeof(Capture));

        freeHashTable(set->table);
        free(set->table);
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
    case astField:
        return node->structField.name;
    default:
        unreachable("Only variables and function params can be captured");
    }
}

AstNode *getCapturedNodeType(const AstNode *node)
{
    switch (node->tag) {
    case astVarDecl:
        return node->varDecl.type;
    case astFuncParam:
        return node->funcParam.type;
    case astField:
        return node->structField.type;
    default:
        unreachable("Only variables and function params can be captured");
    }
}
