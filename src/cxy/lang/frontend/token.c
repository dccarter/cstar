//
// Created by Carter Mbotho on 2024-02-01.
//

#include "token.h"

bool tokenIsKeyword(TokenTag tag)
{
    switch (tag) {
#define f(T, ...) case tok##T:
        KEYWORD_LIST(f)
#undef f
        return true;
    default:
        return false;
    }
}

bool tokenIsPrimitiveType(TokenTag tag)
{
    switch (tag) {
#define f(T, ...) case tok##T:
        PRIM_TYPE_LIST(f)
#undef f
        return true;
    default:
        return false;
    }
}

bool tokenIsIntegerType(TokenTag tag)
{
    switch (tag) {
#define f(T, ...) case tok##T:
        INTEGER_TYPE_LIST(f)
#undef f
        return true;
    default:
        return false;
    }
}

bool tokenIsAssignmentOperator(TokenTag tag)
{
    switch (tag) {
#define f(O, P, T, ...) case tok##T##Equal:
        AST_ASSIGN_EXPR_LIST(f)
#undef f
        return true;
    default:
        return false;
    }
}

PrtId tokenToPrimitiveTypeId(TokenTag tag)
{
    switch (tag) {
#define f(name, ...)                                                           \
    case tok##name:                                                            \
        return prt##name;
        PRIM_TYPE_LIST(f)
#undef f
    default:
        return false;
    }
}
