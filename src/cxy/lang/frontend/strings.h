//
// Created by Carter Mbotho on 2023-07-13.
//

#pragma once

#include "core/utils.h"
#include "operator.h"

// clang-format off
#define CXY_BUILTIN_NAMES(f, ff)    \
    f(main)                         \
    f(__builtins)               \
    f(__cxy_assert)     \
    f(CXY__builtins_sizeof)     \
    f(this)                     \
    f(This)                     \
    f(super)                    \
    f(static)                   \
    f(transient)                \
    f(abstract)                 \
    f(inline)                   \
    f(explicit)                 \
    f(pure)                     \
    f(strlen)                   \
    f(char)                     \
    f(wputc)                    \
    f(cxy_range_t)              \
    f(sb)                       \
    f(s)                        \
    f(__Optional)               \
    f(Slice)                    \
    f(String)                   \
    f(CString)                  \
    f(Iterator)                 \
    ff(_assert, "assert")       \
    f(base_of)                  \
    f(column)                   \
    f(ptr)                      \
    f(cstr)                     \
    f(data)                     \
    f(init)                     \
    f(destructor)               \
    f(file)                     \
    f(len)                      \
    f(line)                     \
    f(mkIdent)                  \
    f(mkInteger)                \
    f(ptroff)                   \
    f(sizeof)                   \
    f(typeof)                   \
    f(allocate)                 \
    f(alias)                    \
    f(name)                     \
    f(None)                     \
    f(Some)                     \
    f(unchecked)                \
    f(_Variadic)                \
    f(consistent)               \
    f(final)                    \
    f(getref)                   \
    f(dropref)                  \
    f(structToString)           \
    f(classToString)            \
    f(computeStructHash)        \
    f(computeClassHash)         \
    f(destructorForward)        \
    f(newClass)                 \
    f(release)                  \
    f(vtable)                   \
    f(poco)                     \
    f(Promise)                  \
    f(Coroutine)                \
    f(asyncLaunch)              \
    f(asyncLaunchMember)        \
    f(sync)                     \
    f(self)                     \
    f(__name)                   \
    f(__fwd)                    \
    f(CXY__main)

// clang-format on

#define f(name, ...) extern cstring S_##name;
CXY_BUILTIN_NAMES(f, f)
AST_UNARY_EXPR_LIST(f)
AST_BINARY_EXPR_LIST(f)
f(Truthy);
#undef f

#define f(name, ...)                                                           \
    extern cstring S_##name;                                                   \
    extern cstring S_##name##_;
AST_OVERLOAD_ONLY_OPS(f)
#undef f
extern cstring S_Deref_;

#define f(name, ...) extern cstring S_##name##_eq;
AST_ASSIGN_EXPR_LIST(f)
#undef f

typedef struct StrPool StrPool;

void internCommonStrings(StrPool *pool);
