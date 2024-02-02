//
// Created by Carter on 2023-06-29.
//

#pragma once

#include <stdbool.h>

#include "operators.def"

// clang-format off

typedef enum {
    opInvalid,
#define f(name, ...) op##name,
    AST_BINARY_EXPR_LIST(f)
    AST_UNARY_EXPR_LIST(f)
#undef f
#define f(name, ...) op##name##Equal,
    opAssign,
    AST_ASSIGN_EXPR_LIST(f)
#undef f

#define f(name, ...) op##name,
    AST_OVERLOAD_ONLY_OPS(f)
#undef f
    opTruthy
} Operator;

// clang-format on

const char *getUnaryOpString(Operator op);
const char *getBinaryOpString(Operator op);
const char *getAssignOpString(Operator op);
const char *getOpOverloadName(Operator op);
const char *getOperatorString(Operator op);

bool isPrefixOpKeyword(Operator op);

int getMaxBinaryOpPrecedence(void);

int getBinaryOpPrecedence(Operator op);