//
// Created by Carter Mbotho on 2024-11-01.
//

#pragma once

#include <core/array.h>
#include <core/utils.h>

struct MirType;
struct Log;
struct MemPool;
struct CompilerDriver;

// clang-format off
#define CXY_MIR_FLAGS(f)        \
    f(None,            0)       \
    f(Const,           1)       \
    f(Reference,       2)       \
    f(Pointer,         3)       \
    f(Extern,          4)       \
    f(Inline,          5)       \
    f(NoInline,        6)       \
    f(NoOptimize,      7)       \
    f(Constructor,     8)       \
    f(Destructor,      9)
// clang-format off

#define f(NAME, VALUE) static u64 mif##NAME = BIT(VALUE);
CXY_MIR_FLAGS(f)
#undef f

typedef struct MirContext {
    struct Log *L;
    struct MemPool *pool;
    struct StrPool *strings;
    struct TypeTable *types;
    DynArray modules;
} MirContext;

MirContext *mirContextCreate(struct CompilerDriver *cc);
void mirContextDestroy(struct CompilerDriver *cc);
u64 astFlagsToMirFlags(u64 flags);
