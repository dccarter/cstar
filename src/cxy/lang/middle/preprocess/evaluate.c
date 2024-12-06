//
// Created by Carter Mbotho on 2024-01-09.
//

#include "preprocessor.h"

#include "lang/frontend/ast.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"

static void evalAddOperation(PreprocessorContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
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

static void evalSubOperation(PreprocessorContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;

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

static void evalMulOperation(PreprocessorContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
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

static void evalDivOperation(PreprocessorContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;

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

static void evalModOperation(PreprocessorContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
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

static bool checkBinaryBitOperation(PreprocessorContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    cstring name = getBinaryOpString(node->binaryExpr.op);

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
    static void eval##Name##Operation(PreprocessorContext *ctx, AstNode *node) \
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

static void evalLAndOperation(PreprocessorContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;

    node->tag = astBoolLit;
    node->boolLiteral.value =
        nodeGetNumericLiteral(lhs) && nodeGetNumericLiteral(rhs);
}

static void evalLOrOperation(PreprocessorContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;

    node->tag = astBoolLit;
    node->boolLiteral.value =
        nodeGetNumericLiteral(lhs) || nodeGetNumericLiteral(rhs);
}

static void evalIsOperation(PreprocessorContext *ctx, AstNode *node)
{
    logError(ctx->L,
             &node->loc,
             "operator `is` is not a valid comptime operator",
             NULL);
}

#define CXY_DEFINE_BINARY_EQ_COMP_OPERATOR(Name, OP)                           \
    static void eval##Name##Operation(PreprocessorContext *ctx, AstNode *node) \
    {                                                                          \
        AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;      \
                                                                               \
        memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);                       \
        node->tag = astBoolLit;                                                \
        node->boolLiteral.value =                                              \
            nodeGetNumericLiteral(lhs) OP nodeGetNumericLiteral(rhs);          \
    }

CXY_DEFINE_BINARY_EQ_COMP_OPERATOR(Eq, ==)

CXY_DEFINE_BINARY_EQ_COMP_OPERATOR(Ne, !=)

#undef CXY_DEFINE_BINARY_EQ_COMP_OPERATOR

#define CXY_DEFINE_BINARY_COMP_OPERATOR(Name, OP)                              \
    static void eval##Name##Operation(PreprocessorContext *ctx, AstNode *node) \
    {                                                                          \
        AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;      \
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

static void evalRangeOperation(PreprocessorContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    if (nodeIs(lhs, IntegerLit) && nodeIs(rhs, IntegerLit)) {
        node->tag = astRangeExpr;
        node->rangeExpr.start = lhs;
        node->rangeExpr.end = rhs;
        node->rangeExpr.step = NULL;
        return;
    }
}

void preprocessorEvalBinaryExpr(PreprocessorContext *ctx, AstNode *node)
{
    switch (node->binaryExpr.op) {
#define f(O, ...)                                                              \
    case op##O:                                                                \
        eval##O##Operation(ctx, node);                                         \
        break;
        AST_BINARY_EXPR_LIST(f)
#undef f
    default:
        return;
    }
}

static void evalPrefixMinusOperation(PreprocessorContext *ctx, AstNode *node)
{
    AstNode *rhs = node->unaryExpr.operand;

    f64 value = -nodeGetNumericLiteral(rhs);
    if (nodeIs(node, FloatLit)) {
        node->tag = astFloatLit;
        node->floatLiteral.value = value;
    }
    else {
        node->tag = astIntegerLit;
        node->intLiteral.isNegative = value < 0;
        if (node->intLiteral.isNegative)
            node->intLiteral.value = (i64)value;
        else
            node->intLiteral.uValue = (u64)value;
    }
}

static void evalPrefixPlusOperation(PreprocessorContext *ctx, AstNode *node)
{
    AstNode *rhs = node->unaryExpr.operand;
    f64 value = +nodeGetNumericLiteral(rhs);
    if (nodeIs(node, FloatLit)) {
        node->tag = astFloatLit;
        node->floatLiteral.value = value;
    }
    else {
        node->tag = astIntegerLit;
        node->intLiteral.isNegative = value < 0;
        if (node->intLiteral.isNegative)
            node->intLiteral.value = (i64)value;
        else
            node->intLiteral.uValue = (u64)value;
    }
}

static void evalLogicNotOperation(PreprocessorContext *ctx, AstNode *node)
{
    AstNode *rhs = node->unaryExpr.operand;
    node->tag = astBoolLit;
    node->boolLiteral.value = !nodeGetNumericLiteral(rhs);
}

static void evalBitNotOperation(PreprocessorContext *ctx, AstNode *node)
{
    AstNode *rhs = node->binaryExpr.lhs;
    if (!nodeIs(rhs, IntegerLit) && !nodeIs(rhs, CharLit))
        return;

    node->tag = astIntegerLit;
    node->intLiteral.uValue = ~((u64)nodeGetNumericLiteral(rhs));
}

void preprocessorEvalUnaryExpr(PreprocessorContext *ctx, AstNode *node)
{
    switch (node->unaryExpr.op) {
    case opPlus:
        evalPrefixPlusOperation(ctx, node);
        break;
    case opMinus:
        evalPrefixMinusOperation(ctx, node);
        break;
    case opNot:
        evalLogicNotOperation(ctx, node);
        break;
    case opCompl:
        evalBitNotOperation(ctx, node);
        break;
    default:
        break;
    }
}

bool preprocessorAsBoolean(PreprocessorContext *ctx, AstNode *node)
{
    switch (node->tag) {
    case astBoolLit:
        break;
    case astNullLit:
        node->boolLiteral.value = false;
        break;
    case astCharLit:
        node->boolLiteral.value = node->charLiteral.value != '\0';
        node->tag = astBoolLit;
        break;
    case astIntegerLit:
        node->boolLiteral.value = integerLiteralValue(node) != 0;
        node->tag = astBoolLit;
        break;
    case astFloatLit:
        node->boolLiteral.value = node->floatLiteral.value != 0;
        node->tag = astBoolLit;
        break;
    case astStringLit:
        node->boolLiteral.value = (node->stringLiteral.value == NULL) ||
                                  (node->stringLiteral.value[0] != '\0');
        node->tag = astBoolLit;
        break;
    default:
        return false;
    }

    return true;
}
