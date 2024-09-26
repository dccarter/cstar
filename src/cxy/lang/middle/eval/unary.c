//
// Created by Carter Mbotho on 2024-01-09.
//

#include "eval.h"

static bool evalVerifyIntegerOperand(EvalContext *ctx,
                                     AstNode *node,
                                     Operator op)
{
    if (!nodeIs(node, IntegerLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operand of prefix {s} must be an integer",
                 (FormatArg[]){{.s = getUnaryOpString(op)}});

        node->tag = astError;
        return false;
    }
    return true;
}

static bool evalVerifyIntegerOrFloatOperand(EvalContext *ctx,
                                            AstNode *node,
                                            Operator op)
{
    if (!nodeIs(node, IntegerLit) && !nodeIs(node, FloatLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operand of prefix {s} must be either an integer or "
                 "a float",
                 (FormatArg[]){{.s = getUnaryOpString(op)}});

        node->tag = astError;
        return false;
    }
    return true;
}

static void evalPrefixMinusOperation(EvalContext *ctx, AstNode *node)
{
    AstNode *rhs = node->unaryExpr.operand;
    if (!evalVerifyIntegerOrFloatOperand(ctx, rhs, node->unaryExpr.op))
        return;

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

static void evalPrefixPlusOperation(EvalContext *ctx, AstNode *node)
{
    AstNode *rhs = node->unaryExpr.operand;
    if (!evalVerifyIntegerOrFloatOperand(ctx, rhs, node->unaryExpr.op))
        return;

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

static void evalLogicNotOperation(EvalContext *ctx, AstNode *node)
{
    AstNode *rhs = node->unaryExpr.operand;
    if (!evalBooleanCast(ctx, rhs))
        return;

    node->tag = astBoolLit;
    node->boolLiteral.value = !rhs->boolLiteral.value;
}

static void evalBitNotOperation(EvalContext *ctx, AstNode *node)
{
    AstNode *rhs = node->binaryExpr.lhs;
    if (!nodeIs(rhs, IntegerLit) && !nodeIs(rhs, CharLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operand of prefix `~` must be of integral type",
                 NULL);

        node->tag = astError;
        return;
    }

    node->tag = astIntegerLit;
    node->intLiteral.uValue = ~((u64)nodeGetNumericLiteral(rhs));
}

void evalUnaryExpr(AstVisitor *visitor, AstNode *node)
{
    attr(unused) EvalContext *ctx = getAstVisitorContext(visitor);
    attr(unused) AstNode *next = node->next, *parentScope = node->parentScope;
    if (!evaluate(visitor, node->unaryExpr.operand)) {
        node->tag = astError;
        return;
    }

    if (!node->unaryExpr.isPrefix) {
        logError(ctx->L,
                 &node->loc,
                 "postfix unary operator `{s}` is not supported at comptime",
                 (FormatArg[]){{.s = getUnaryOpString(node->unaryExpr.op)}});
        return;
    }

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
    case opBAnd:
        evalBitNotOperation(ctx, node);
        break;
    default:
        logError(ctx->L,
                 &node->loc,
                 "prefix unary operator `{s}` is not supported at comptime",
                 (FormatArg[]){{.s = getUnaryOpString(node->unaryExpr.op)}});
        node->tag = astError;
    }
}
