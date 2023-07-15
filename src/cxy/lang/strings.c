//
// Created by Carter Mbotho on 2023-07-13.
//

#include "strings.h"
#include "core/strpool.h"

#define f(name, ...) cstring S_##name = NULL;
CXY_BUILTIN_NAMES(f, f)
AST_UNARY_EXPR_LIST(f)
AST_BINARY_EXPR_LIST(f)
AST_OVERLOAD_ONLY_OPS(f)
#undef f
#define f(name, ...) cstring S_##name##_eq = NULL;
AST_ASSIGN_EXPR_LIST(f)
#undef f

void internCommonStrings(StrPool *pool)
{
#define f(name) S_##name = makeString(pool, #name);
#define ff(name, str) S_##name = makeString(pool, str);
    CXY_BUILTIN_NAMES(f, ff);
#undef f

#define f(name, _0, _1, str) S_##name = makeString(pool, str);
    AST_PREFIX_EXPR_LIST(f)
#undef f

#define f(name, _0, _1, _2, str) S_##name = makeString(pool, str);
    AST_BINARY_EXPR_LIST(f)
#undef f

#define f(name, _0, _1, _2, str) S_##name##_eq = makeString(pool, str "_eq");
    AST_ASSIGN_EXPR_LIST(f)
#undef f

#define f(name, str) S_##name = makeString(pool, str);
    AST_OVERLOAD_ONLY_OPS(f)
#undef f
}