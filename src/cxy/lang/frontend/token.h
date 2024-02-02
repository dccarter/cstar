// credits: https://github.com/madmann91/fu/blob/master/src/fu/lang/token.h
#pragma once

#include "core/log.h"
#include "core/utils.h"

#include "tokens.def"

#include "operator.h"
#include "primitives.h"

typedef enum {
    tokInvalid,
#define f(name, ...) tok##name,
    TOKEN_LIST(f)
#undef f
        tokAssignEqual = tokAssign
} TokenTag;

// clang-format on

typedef struct {
    TokenTag tag;
    union {
        uintmax_t iVal;
        intmax_t uVal;
        double fVal;
        u32 cVal;
    };
    FileLoc fileLoc;
} Token;

static inline const char *token_tag_to_str(TokenTag tag)
{
    switch (tag) {
#define f(name, str)                                                           \
    case tok##name:                                                            \
        return str;
#define g(name, str, ...)                                                      \
    case tok##name:                                                            \
        return "'" str "'";
        SYMBOL_LIST(g)
        KEYWORD_LIST(g)
        SPECIAL_TOKEN_LIST(f)
#undef g
#undef f
    default:
        return NULL;
    }
}

bool tokenIsKeyword(TokenTag tag);
bool tokenIsPrimitiveType(TokenTag tag);
bool tokenIsIntegerType(TokenTag tag);
bool tokenIsAssignmentOperator(TokenTag tag);
PrtId tokenToPrimitiveTypeId(TokenTag tag);
Operator tokenToUnaryOperator(TokenTag tag);
Operator tokenToPostfixUnaryOperator(TokenTag tag);
Operator tokenToBinaryOperator(TokenTag tag);
Operator tokenToAssignmentOperator(TokenTag tag);