//
// Created by Carter Mbotho on 2023-07-13.
//

#include "strings.h"
#include "core/strpool.h"

#define f(name, ...) cstring S_##name = NULL;
CXY_BUILTIN_NAMES(f, f)
AST_UNARY_EXPR_LIST(f)
AST_BINARY_EXPR_LIST(f)
OVERRIDABLE_BUILTINS(f)
f(Truthy);
#undef f

#define f(name, ...)                                                           \
    cstring S_##name = NULL;                                                   \
    cstring S_##name##_ = NULL;
AST_OVERLOAD_ONLY_OPS(f)
#undef f
cstring S_Deref_;

#define f(name, ...) cstring S_##name##_eq = NULL;
AST_ASSIGN_EXPR_LIST(f)
#undef f

void internCommonStrings(StrPool *pool)
{
#define f(name) S_##name = makeString(pool, #name);
#define ff(name, str) S_##name = makeString(pool, str);
    CXY_BUILTIN_NAMES(f, ff);
#undef ff
    OVERRIDABLE_BUILTINS(f)
#undef f

#define f(name, _0, _1, str) S_##name = makeString(pool, "op__" str);
    AST_PREFIX_EXPR_LIST(f)
#undef f

#define f(name, _0, _1, _2, str) S_##name = makeString(pool, "op__" str);
    AST_BINARY_EXPR_LIST(f)
#undef f

#define f(name, _0, _1, _2, str)                                               \
    S_##name##_eq = makeString(pool, "op__" str "_eq");
    AST_ASSIGN_EXPR_LIST(f)
#undef f

#define f(name, str, ...)                                                      \
    S_##name = makeString(pool, "op__" str);                                   \
    S_##name##_ = makeString(pool, str);
    AST_OVERLOAD_ONLY_OPS(f)
#undef f
    S_Deref_ = makeString(pool, "deref");
    S_Truthy = makeString(pool, "op__truthy");
}