//
// Created by Carter on 2023-06-29.
//

#pragma once

#include "core/format.h"
#include "core/strpool.h"
#include "core/utils.h"

#ifdef __cplusplus
extern "C" {
#endif

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
    f(MacroArgument,        13)                 \
    f(ClosureStyle,         14)                 \
    f(FuncTypeParam,        15)                 \
    f(Member,               16)                 \
    f(AddThis,              17)                 \
    f(AddSuper,             18)                 \
    f(Typeinfo,             19)                 \
    f(ForwardDecl,          20)                 \
    f(Reference,            21)                 \
    f(TopLevelDecl,         22)                 \
    f(Generated,            23)                 \
    f(Executable,           24)                 \
    f(ImportAlias,          25)                 \
    f(EnumLiteral,          26)                 \
    f(Comptime,             27)                 \
    f(Visited,              28)                 \
    f(ImplementsDeinit,     29)                 \
    f(Temporary,            30)                 \
    f(Unsafe,               31)                 \
    f(FunctionPtr,          32)                 \
    f(Returned,             33)                 \
    f(ComptimeIterable,     34)                 \
    f(Define,               35)                 \
    f(UnsafeCast,           36)                 \
    f(Extern,               37)                 \
    f(Inherited,            38)                 \
    f(BlockReturns,         39)                 \
    f(BuiltinsModule,       40)                 \
    f(Optional,             41)                 \
    f(ImportedModule,       42)                 \
    f(ReferenceMembers,     43)                 \
    f(Pure,                 44)                 \
    f(Slice,                45)                 \
    f(Transient,            46)                 \
    f(Static,               47)                 \
    f(SyncCall,             48)                 \
    f(VTable,               49)                 \
    f(Substituted,          50)                 \
    f(Virtual,              51)                 \
    f(Abstract,             52)                 \
    f(ModuleInit,           53)                 \
    f(Move,                 54)                 \
    f(Moved,                55)                 \
    f(BlockValue,           56)                 \
    f(DiDisable,            57)                 \
    f(DefaultedFields,      58)                 \
    f(Anonymous,            59)                 \
    f(Constructor,          60)                 \
    f(TestContext,          61)

// clang-format on
static const u64 flgNone = 0;
#define f(name, bit) static const u64 flg##name = BIT(bit);
CXY_LANG_FLAGS(f)
#undef f
static const u64 flgTypeApplicable =
    (flgExtern | flgConst | flgOptional | flgClosure | flgSlice |
     flgFuncTypeParam | flgReferenceMembers);

typedef u64 Flags;

void appendFlagsAsString(FormatState *state, u64 flags);
char *flagsToString(u64 flags);

#ifdef __cplusplus
}
#endif
