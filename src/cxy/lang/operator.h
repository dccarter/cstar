//
// Created by Carter on 2023-06-29.
//

#pragma once

#include <lang/token.h>

// clang-format off

#define AST_ARITH_EXPR_LIST(f)                     \
    f(Add, 3, Plus, "+", "add")                    \
    f(Sub, 3, Minus,"-", "sub")                   \
    f(Mul, 2, Mult, "*", "mul")                    \
    f(Div, 2, Div,  "/", "div")                   \
    f(Mod, 2, Mod,  "%", "rem")

#define AST_BIT_EXPR_LIST(f)                        \
    f(BAnd, 7, BAnd, "&", "and")                    \
    f(BOr,  9, BOr,  "|", "or")                     \
    f(BXor, 8, BXor, "^", "xor")

#define AST_SHIFT_EXPR_LIST(f)         \
    f(Shl, 4, Shl, "<<", "lshift")     \
    f(Shr, 4, Shr, ">>", "rshift")

#define AST_CMP_EXPR_LIST(f)                   \
    f(Eq,  6, Equal,        "==", "eq")        \
    f(Ne,  6, NotEqual,     "!=", "neq")       \
    f(Gt,  5, Greater,      ">", "gt")         \
    f(Lt,  5, Less,         "<", "lt")         \
    f(Geq, 5, GreaterEqual, ">=", "geq")       \
    f(Leq, 5, LessEqual,    "<=", "leq")

#define AST_LOGIC_EXPR_LIST(f)                \
    f(LAnd, 10, LAnd, "&&", "land")             \
    f(LOr,  11, LOr,  "||", "lor")

#define AST_BINARY_EXPR_LIST(f)           \
    f(Range, 13,DotDot, "..", "range")    \
    AST_ARITH_EXPR_LIST(f)                \
    AST_BIT_EXPR_LIST(f)                  \
    AST_SHIFT_EXPR_LIST(f)                \
    AST_CMP_EXPR_LIST(f)                  \
    AST_LOGIC_EXPR_LIST(f)

#define AST_ASSIGN_EXPR_LIST(f)          \
    f(Assign, 0, Assign, "",  "assign")  \
    AST_ARITH_EXPR_LIST(f)               \
    AST_BIT_EXPR_LIST(f)                 \
    AST_SHIFT_EXPR_LIST(f)

#define AST_PREFIX_EXPR_LIST(f)                     \
    f(PreDec, MinusMinus, "--", "pre_dec")          \
    f(PreInc, PlusPlus, "++", "pre_inc")            \
    f(AddrOf, BAnd, "&", "addroff")                 \
    f(Move,   LAnd, "&&", "move")                   \
    f(Deref,  Mult, "*",  "deref")                  \
    f(Minus,  Minus, "-", "pre_minus")              \
    f(Plus,   Plus, "+",  "pre_plus")               \
    f(Not,    LNot, "!",  "lnot")                   \
    f(Compl,  BNot, "~",  "bnot")                   \
    f(Spread, Elipsis, "...", "spread")             \
    f(New,    New,  "new",    "new")                \
    f(Await,  Await, "await", "await")              \
    f(Delete, Delete,"delete", "delete")            \

#define AST_POSTFIX_EXPR_LIST(f)                \
    f(PostDec, MinusMinus, "--", "dec")         \
    f(PostInc, PlusPlus,  "++", "inc")

#define AST_UNARY_EXPR_LIST(f)                                                 \
    AST_PREFIX_EXPR_LIST(f)                                                    \
    AST_POSTFIX_EXPR_LIST(f)

#define AST_OVERLOAD_ONLY_OPS(f)                         \
    f(CallOverload,             "call")                  \
    f(IndexOverload,            "idx")                   \
    f(IndexAssignOverload,      "idx_assign")            \
    f(StringOverload,           "str")


typedef enum {
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
    opTruthy,
    opInvalid
} Operator;

// clang-format on

const char *getUnaryOpString(Operator op);
const char *getBinaryOpString(Operator op);
const char *getAssignOpString(Operator op);
const char *getBinaryOpFuncName(Operator op);

Operator tokenToUnaryOperator(TokenTag tag);

Operator tokenToBinaryOperator(TokenTag tag);

Operator tokenToAssignmentOperator(TokenTag tag);

bool isPrefixOpKeyword(Operator op);

int getMaxBinaryOpPrecedence(void);

int getBinaryOpPrecedence(Operator op);