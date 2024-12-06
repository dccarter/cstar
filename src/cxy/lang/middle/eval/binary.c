//
// Created by Carter Mbotho on 2024-01-09.
//

#include "eval.h"

#include "lang/frontend/strings.h"

#include "core/strpool.h"

static inline bool isSupportedBinaryOperand(AstNode *node)
{
    node = nodeIs(node, Ref) ? node->reference.target : node;
    return isLiteralExpr(node) || isTypeExpr(node) ||
           nodeIs(node, GenericParam);
}

static inline bool isArgumentTypeThis(const AstNode *node)
{
    return nodeIs(node, Path) &&
           node->path.elements->pathElement.name == S_This;
}

bool evalStringBuilderAppend(EvalContext *ctx, StringBuilder *sb, AstNode *node)
{
    switch (node->tag) {
    case astStringLit:
        stringBuilderAppendCstr1(sb, node->stringLiteral.value);
        break;
    case astIntegerLit:
        if (node->intLiteral.isNegative)
            stringBuilderAppendChar(sb, '-');
        stringBuilderAppendUInt(sb, node->intLiteral.uValue);
        break;
    case astFloatLit:
        stringBuilderAppendFloat(sb, node->floatLiteral.value);
        break;
    case astCharLit:
        stringBuilderAppendChar(sb, node->charLiteral.value);
        break;
    case astBoolLit:
        stringBuilderAppendBool(sb, node->boolLiteral.value);
        break;
    default:
        logError(ctx->L,
                 &node->loc,
                 "expression cannot be transformed to a string",
                 NULL);
        return false;
    }

    return true;
}

void evalStringConcatenation(EvalContext *ctx,
                             AstNode *node,
                             AstNode *lhs,
                             AstNode *rhs)
{
    StringBuilder sb;
    stringBuilderInit(&sb);
    evalStringBuilderAppend(ctx, &sb, lhs);
    csAssert0(evalStringBuilderAppend(ctx, &sb, rhs));

    char *str = stringBuilderRelease(&sb);
    node->tag = astStringLit;
    node->stringLiteral.value = makeString(ctx->strings, str);
    free(str);
}

bool verifyBinaryExprOperand(EvalContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs;
    AstNode *rhs = node->binaryExpr.rhs;
    if (!isSupportedBinaryOperand(lhs) || !isSupportedBinaryOperand(rhs)) {
        logError(ctx->L,
                 &lhs->loc,
                 "operands of `{s}` must be either a literal or type at "
                 "compile time",
                 (FormatArg[]){{.s = getBinaryOpString(node->binaryExpr.op)}});
        node->tag = astError;
        return false;
    }

    return true;
}

static void evalAddOperation(EvalContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    if (nodeIs(lhs, StringLit)) {
        evalStringConcatenation(ctx, node, lhs, rhs);
        return;
    }

    if (nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "RHS operand of comp-time `+` operation can only "
                 "be a string when LHS is a string",
                 NULL);

        node->tag = astError;
        return;
    }

    if (nodeIs(lhs, BoolLit) && nodeIs(rhs, BoolLit)) {
        logWarning(
            ctx->L,
            &node->loc,
            "comp-time addition between boolean operands doesn't make sense",
            NULL);

        node->tag = astError;
        return;
    }

    f64 value = nodeGetNumericLiteral(lhs) + nodeGetNumericLiteral(rhs);
    nodeSetNumericLiteral(node, lhs, rhs, value);
}

static void evalSubOperation(EvalContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `-` cannot be a string",
                 NULL);

        node->tag = astError;
        return;
    }

    if (nodeIs(lhs, BoolLit) && nodeIs(rhs, BoolLit)) {
        logWarning(
            ctx->L,
            &node->loc,
            "comp-time subtraction between boolean operands doesn't make sense",
            NULL);
    }

    f64 value = nodeGetNumericLiteral(lhs) - nodeGetNumericLiteral(rhs);
    nodeSetNumericLiteral(node, lhs, rhs, value);
}

static void evalMulOperation(EvalContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `*` cannot be a string",
                 NULL);

        node->tag = astError;
        return;
    }

    if (nodeIs(lhs, BoolLit) || nodeIs(rhs, BoolLit)) {
        logWarning(ctx->L,
                   &node->loc,
                   "comp-time multiplication with boolean operands doesn't "
                   "make sense",
                   NULL);
    }
    else if (nodeIs(lhs, CharLit) || nodeIs(rhs, CharLit)) {
        logWarning(ctx->L,
                   &node->loc,
                   "comp-time multiplication with character operands doesn't "
                   "make sense",
                   NULL);
    }

    f64 value = nodeGetNumericLiteral(lhs) * nodeGetNumericLiteral(rhs);
    nodeSetNumericLiteral(node, lhs, rhs, value);
}

static void evalDivOperation(EvalContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `/` cannot be a string",
                 NULL);

        node->tag = astError;
        return;
    }

    if (nodeIs(lhs, BoolLit) || nodeIs(rhs, BoolLit)) {
        logWarning(ctx->L,
                   &node->loc,
                   "comp-time division with boolean operands doesn't "
                   "make sense",
                   NULL);
    }
    else if (nodeIs(lhs, CharLit) || nodeIs(rhs, CharLit)) {
        logWarning(ctx->L,
                   &node->loc,
                   "comp-time division with character operands doesn't "
                   "make sense",
                   NULL);
    }

    if (nodeIs(lhs, FloatLit) || nodeIs(rhs, FloatLit)) {
        node->tag = astFloatLit;
        node->floatLiteral.value =
            nodeGetNumericLiteral(lhs) / nodeGetNumericLiteral(rhs);
    }
    else {
        i64 value =
            (i64)nodeGetNumericLiteral(lhs) / (i64)nodeGetNumericLiteral(rhs);
        nodeSetNumericLiteral(node, lhs, rhs, (f64)value);
    }
}

static void evalModOperation(EvalContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `%` cannot be a string",
                 NULL);

        node->tag = astError;
        return;
    }

    if (nodeIs(lhs, FloatLit) || nodeIs(rhs, FloatLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operand(s) of `%` cannot be float",
                 NULL);

        node->tag = astError;
        return;
    }

    nodeSetNumericLiteral(node,
                          lhs,
                          rhs,
                          (f64)((i64)nodeGetNumericLiteral(lhs) %
                                (i64)nodeGetNumericLiteral(rhs)));
}

static bool checkBinaryBitOperation(EvalContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    cstring name = getBinaryOpString(node->binaryExpr.op);
    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `{s}` cannot be of type string",
                 (FormatArg[]){{.s = name}});

        node->tag = astError;
        return false;
    }

    if (nodeIs(lhs, FloatLit) || nodeIs(rhs, FloatLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `{s}` cannot be float",
                 (FormatArg[]){{.s = name}});

        node->tag = astError;
        return false;
    }

    if (nodeIs(lhs, BoolLit) || nodeIs(rhs, BoolLit)) {
        logWarning(
            ctx->L,
            &node->loc,
            "comp-time binary operator `{s}' with boolean operands doesn't "
            "make sense",
            (FormatArg[]){{.s = name}});
    }
    else if (nodeIs(lhs, CharLit) || nodeIs(rhs, CharLit)) {
        logWarning(
            ctx->L,
            &node->loc,
            "comp-time binary operator '{s}' with character operands doesn't "
            "make sense",
            (FormatArg[]){{.s = name}});
    }

    return true;
}

#define CXY_DEFINE_BINARY_BIT_OPERATOR(Name, OP)                               \
    static void eval##Name##Operation(EvalContext *ctx, AstNode *node)         \
    {                                                                          \
        AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;      \
        if (!checkBinaryBitOperation(ctx, node))                               \
            return;                                                            \
                                                                               \
        nodeSetNumericLiteral(node,                                            \
                              lhs,                                             \
                              rhs,                                             \
                              (f64)((i64)nodeGetNumericLiteral(lhs) OP(i64)    \
                                        nodeGetNumericLiteral(rhs)));          \
    }

CXY_DEFINE_BINARY_BIT_OPERATOR(Shl, <<)

CXY_DEFINE_BINARY_BIT_OPERATOR(Shr, >>)

CXY_DEFINE_BINARY_BIT_OPERATOR(BAnd, &)

CXY_DEFINE_BINARY_BIT_OPERATOR(BOr, |)

CXY_DEFINE_BINARY_BIT_OPERATOR(BXor, ^)

static void evalLAndOperation(EvalContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `&&` cannot be of type string",
                 NULL);

        node->tag = astError;
        return;
    }

    node->tag = astBoolLit;
    node->boolLiteral.value =
        nodeGetNumericLiteral(lhs) && nodeGetNumericLiteral(rhs);
}

static void evalLOrOperation(EvalContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `&&` cannot be of type string",
                 NULL);

        node->tag = astError;
        return;
    }

    node->tag = astBoolLit;
    node->boolLiteral.value =
        nodeGetNumericLiteral(lhs) || nodeGetNumericLiteral(rhs);
}

static bool checkComparisonOperation(EvalContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "unsupported comp-time operation '{s}' between a string and a "
                 "non-string literal",
                 (FormatArg[]){{.s = getBinaryOpString(node->binaryExpr.op)}});

        node->tag = astError;
        return false;
    }

    if (isTypeExpr(lhs) || isTypeExpr(rhs)) {
        logError(ctx->L,
                 &node->loc,
                 "unsupported comp-time operation '{s}' between a type and a "
                 "non type literal",
                 (FormatArg[]){{.s = getBinaryOpString(node->binaryExpr.op)}});

        node->tag = astError;
        return false;
    }

    return true;
}

#define CXY_DEFINE_BINARY_EQ_COMP_OPERATOR(Name, OP)                           \
    static void eval##Name##Operation(EvalContext *ctx, AstNode *node)         \
    {                                                                          \
        AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;      \
        if (nodeIs(lhs, StringLit) && nodeIs(rhs, StringLit)) {                \
            memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);                   \
            node->tag = astBoolLit;                                            \
            node->boolLiteral.value =                                          \
                lhs->stringLiteral.value OP rhs->stringLiteral.value;          \
            return;                                                            \
        }                                                                      \
        if (isTypeExpr(lhs) && isTypeExpr(rhs)) {                              \
            memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);                   \
            node->tag = astBoolLit;                                            \
            node->boolLiteral.value = true OP comptimeCompareTypes(lhs, rhs);  \
            return;                                                            \
        }                                                                      \
                                                                               \
        if (!checkComparisonOperation(ctx, node))                              \
            return;                                                            \
                                                                               \
        memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);                       \
        node->tag = astBoolLit;                                                \
        node->boolLiteral.value =                                              \
            nodeGetNumericLiteral(lhs) OP nodeGetNumericLiteral(rhs);          \
    }

static void evalIsOperation(EvalContext *ctx, AstNode *node)
{
    logError(ctx->L,
             &node->loc,
             "operator `is` is not a valid comptime operator",
             NULL);
}

CXY_DEFINE_BINARY_EQ_COMP_OPERATOR(Eq, ==)

CXY_DEFINE_BINARY_EQ_COMP_OPERATOR(Ne, !=)

#undef CXY_DEFINE_BINARY_EQ_COMP_OPERATOR

#define CXY_DEFINE_BINARY_COMP_OPERATOR(Name, OP)                              \
    static void eval##Name##Operation(EvalContext *ctx, AstNode *node)         \
    {                                                                          \
        AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;      \
        if (nodeIs(lhs, StringLit) && nodeIs(rhs, StringLit)) {                \
            memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);                   \
            node->tag = astBoolLit;                                            \
            node->boolLiteral.value = strcmp(lhs->stringLiteral.value,         \
                                             rhs->stringLiteral.value) OP 0;   \
            return;                                                            \
        }                                                                      \
                                                                               \
        if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {                \
            logError(ctx->L,                                                   \
                     &node->loc,                                               \
                     "unsupported comp-time operation " #OP                    \
                     " between a string and a "                                \
                     "non-string literal",                                     \
                     NULL);                                                    \
                                                                               \
            node->tag = astError;                                              \
            return;                                                            \
        }                                                                      \
                                                                               \
        memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);                       \
        node->tag = astBoolLit;                                                \
        node->boolLiteral.value =                                              \
            nodeGetNumericLiteral(lhs) OP nodeGetNumericLiteral(rhs);          \
    }

CXY_DEFINE_BINARY_COMP_OPERATOR(Lt, <)

CXY_DEFINE_BINARY_COMP_OPERATOR(Gt, >)

CXY_DEFINE_BINARY_COMP_OPERATOR(Leq, <=)

CXY_DEFINE_BINARY_COMP_OPERATOR(Geq, >=)

#undef CXY_DEFINE_BINARY_COMP_OPERATOR

static void evalRangeOperation(EvalContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    if (nodeIs(lhs, IntegerLit) && nodeIs(rhs, IntegerLit)) {
        node->tag = astRangeExpr;
        node->rangeExpr.start = lhs;
        node->rangeExpr.end = rhs;
        node->rangeExpr.step = NULL;
        return;
    }

    logError(ctx->L,
             &node->loc,
             "unsupported comp-time operation '..', range start and stop must "
             "be compile time expressions",
             NULL);

    node->tag = astError;
}

void evalBinaryExpr(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    AstNode *next = node->next, *parentScope = node->parentScope;
    if (!evaluate(visitor, node->binaryExpr.lhs)) {
        node->tag = astError;
        return;
    }

    if (!evaluate(visitor, node->binaryExpr.rhs)) {
        node->tag = astError;
        return;
    }

    if (!verifyBinaryExprOperand(ctx, node))
        return;

    evalType(ctx, node->binaryExpr.rhs);
    switch (node->binaryExpr.op) {
#define f(O, ...)                                                              \
    case op##O:                                                                \
        eval##O##Operation(ctx, node);                                         \
        break;
        AST_BINARY_EXPR_LIST(f)
#undef f
    default:
        unreachable("NOT supported binary operator")
    }

    node->next = next;
    node->parentScope = parentScope;
}
