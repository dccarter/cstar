//
// Created by Carter on 2023-06-29.
//

#pragma once

#include <core/utils.h>

// clang-format off
#define CXY_LANG_FLAGS(f)                       \
    f(Native,               0)                  \
    f(Builtin,              1)                  \
    f(Public,               2)                  \
    f(Private,              3)                  \
    f(Async,                4)                  \
    f(TypeAst,              5)                  \
    f(Main,                 6)                  \
    f(Variadic,             7)                  \
    f(Const,                8)                  \
    f(Default,              9)                  \
    f(Deferred,             10)                 \
    f(Capture,              11)                 \
    f(Closure,              12)                 \
    f(CapturePointer,       13)                 \
    f(ClosureStyle,         14)                 \
    f(FuncTypeParam,        15)                 \
    f(Member,               16)                 \
    f(AddThis,              17)                 \
    f(AddSuper,             18)                 \
    f(Typeinfo,             19)                 \
    f(NewAllocated,         20)                 \
    f(AppendNS,             21)                 \
    f(TopLevelDecl,         22)                 \
    f(Generated,            23)                 \
    f(CodeGenerated,        24)                 \
    f(ImportAlias,          25)                 \
    f(EnumLiteral,          26)                 \
    f(Comptime,             27)                 \
    f(Visited,              28)                 \
    f(ImplementsDelete,     28)                 \
    f(ImmediatelyReturned,  29)                 \
    f(Unsafe,               30)                 \
    f(FunctionPtr,          31)                 \
    f(BuiltinMember,        32)                 \
    f(ComptimeIterable,     33)                 \
    f(DebugBreak,           33)                 \
    f(Define,               34)                 \
    f(CPointerCast,         35)

// clang-format on

typedef enum {
    flgNone = 0,
#define f(name, bit) flg##name = BIT(bit),
    CXY_LANG_FLAGS(f)
#undef f
} Flags;
