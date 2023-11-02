//
// Created by Carter on 2023-06-29.
//

#include "operator.h"
#include "flag.h"
#include "strings.h"

const char *getUnaryOpString(Operator op)
{
    switch (op) {
#define f(name, _, str, ...)                                                   \
    case op##name:                                                             \
        return str;
        AST_UNARY_EXPR_LIST(f)
#undef f
    default:
        csAssert0(false);
    }
}

const char *getBinaryOpString(Operator op)
{
    switch (op) {
#define f(name, p, t, s, ...)                                                  \
    case op##name:                                                             \
        return s;
        AST_BINARY_EXPR_LIST(f)
#undef f
    default:
        csAssert0(false);
    }
}

const char *getAssignOpString(Operator op)
{
    switch (op) {
#define f(name, p, t, s, ...)                                                  \
    case op##name:                                                             \
        return s "=";
        AST_ASSIGN_EXPR_LIST(f)
#undef f
    default:
        csAssert0(false);
    }
}

const char *getOpOverloadName(Operator op)
{
    switch (op) {
#define f(NAME, ...)                                                           \
    case op##NAME:                                                             \
        return S_##NAME;
        AST_BINARY_EXPR_LIST(f)
        AST_UNARY_EXPR_LIST(f)
        AST_OVERLOAD_ONLY_OPS(f)
#undef f

    case opTruthy:
        return S_Truthy;
    default:
        csAssert0(false);
    }
}

int getMaxBinaryOpPrecedence(void)
{
    static int maxPrecedence = -1;
    if (maxPrecedence < 1) {
        const int precedenceList[] = {
#define f(n, prec, ...) prec,
            AST_BINARY_EXPR_LIST(f)
#undef f
        };
        for (int i = 0; i < (sizeof(precedenceList) / sizeof(int)); i++) {
            maxPrecedence = MAX(maxPrecedence, precedenceList[i]);
        }
        maxPrecedence += 1;
    }

    return maxPrecedence;
}

int getBinaryOpPrecedence(Operator op)
{
    switch (op) {
#define f(name, prec, ...)                                                     \
    case op##name:                                                             \
        return prec;
        // NOLINTBEGIN
        AST_BINARY_EXPR_LIST(f);
        // NOLINTEND
#undef f
    default:
        return getMaxBinaryOpPrecedence();
    }
}

bool isAssignmentOperator(TokenTag tag)
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

Operator tokenToUnaryOperator(TokenTag tag)
{
    switch (tag) {
#define f(O, T, ...)                                                           \
    case tok##T:                                                               \
        return op##O;
        AST_PREFIX_EXPR_LIST(f);
#undef f
    default:
        csAssert(false, "expecting unary operator");
    }
}

Operator tokenToBinaryOperator(TokenTag tag)
{
    switch (tag) {
#define f(O, P, T, ...)                                                        \
    case tok##T:                                                               \
        return op##O;
        AST_BINARY_EXPR_LIST(f);
#undef f
    default:
        return opInvalid;
    }
}

Operator tokenToAssignmentOperator(TokenTag tag)
{
    switch (tag) {
#define f(O, P, T, ...)                                                        \
    case tok##T##Equal:                                                        \
        return op##O;
        AST_ASSIGN_EXPR_LIST(f);
#undef f
    default:
        csAssert(false, "expecting binary operator");
    }
}

bool isPrefixOpKeyword(Operator op)
{
    switch (op) {
#define f(O, T, ...)                                                           \
    case op##O:                                                                \
        return isKeyword(tok##T);
        AST_PREFIX_EXPR_LIST(f)
#undef f
    default:
        return false;
    }
}

static void appendFlagName(FormatState *state, u64 index)
{
    switch (index) {
#define f(name, IDX)                                                           \
    case IDX:                                                                  \
        format(state, #name, NULL);                                            \
        break;
        CXY_LANG_FLAGS(f)
#undef f
    default:
        format(state, "flg_{u64}", (FormatArg[]){{.u64 = index}});
        break;
    }
}

void appendFlagsAsString(FormatState *state, u64 flags)
{
    int index = 0;
    bool first = true;
    while (flags >> index) {
        if (flags & (1ull << index)) {
            if (!first)
                format(state, "|", NULL);
            appendFlagName(state, index);
            first = false;
        }
        index++;
    }
}

char *flagsToString(u64 flags)
{
    FormatState state = newFormatState("", true);
    format(&state, "(", NULL);
    appendFlagsAsString(&state, flags);
    format(&state, ")", NULL);

    char *str = formatStateToString(&state);
    freeFormatState(&state);
    return str;
}
