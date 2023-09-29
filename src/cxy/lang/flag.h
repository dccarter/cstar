//
// Created by Carter on 2023-06-29.
//

#pragma once

#include <core/strpool.h>
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
    f(ImplementsDeinit,     29)                 \
    f(ImmediatelyReturned,  30)                 \
    f(Unsafe,               31)                 \
    f(FunctionPtr,          32)                 \
    f(BuiltinMember,        33)                 \
    f(ComptimeIterable,     34)                 \
    f(Define,               35)                 \
    f(CPointerCast,         36)                 \
    f(DeclarationOnly,      37)                 \
    f(Inherited,            38)                 \
    f(BlockReturns,         39)                 \
    f(BuiltinsModule,       40)                 \
    f(Optional,             41)                 \
    f(ImportedModule,       42)                 \
    f(ReferenceMembers,     43)                 \
    f(Pure,                 44)                 \
    f(Slice,                45)

// clang-format on
static const u64 flgNone = 0;
#define f(name, bit) static const u64 flg##name = BIT(bit);
CXY_LANG_FLAGS(f)
#undef f
static const u64 flgTypeApplicable =
    (flgConst | flgOptional | flgClosure | flgSlice);

typedef u64 Flags;

char *flagsToString(u64 flags);
