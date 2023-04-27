//
// Created by Carter on 2023-04-26.
//

#include "lang/semantics.h"

#include "lang/eval.h"
#include "lang/ttable.h"

#include <core/sb.h>

#include <string.h>

static bool verifyLiteralExpressions(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs;
    AstNode *rhs = node->binaryExpr.rhs;
    if (!isLiteralExpr(lhs) || !isLiteralExpr(rhs)) {
        logError(ctx->L,
                 &lhs->loc,
                 "operands of `{s}` must be a literal at compile time",
                 (FormatArg[]){{.s = getBinaryOpString(node->binaryExpr.op)}});
        node->tag = astError;
        return false;
    }

    return true;
}

static void evalStringConcatenation(SemanticsContext *ctx,
                                    AstNode *node,
                                    AstNode *lhs,
                                    AstNode *rhs)
{
    StringBuilder sb;
    stringBuilderInit(&sb);
    stringBuilderAppendCstr1(&sb, lhs->stringLiteral.value);
    switch (rhs->tag) {
    case astStringLit:
        stringBuilderAppendCstr1(&sb, rhs->stringLiteral.value);
        break;
    case astIntegerLit:
        if (rhs->intLiteral.hasMinus)
            stringBuilderAppendChar(&sb, '-');
        stringBuilderAppendInt(&sb, rhs->intLiteral.value);
        break;
    case astFloatLit:
        stringBuilderAppendFloat(&sb, rhs->floatLiteral.value);
        break;
    case astCharLit:
        stringBuilderAppendChar(&sb, rhs->charLiteral.value);
        break;
    case astBoolLit:
        stringBuilderAppendBool(&sb, rhs->boolLiteral.value);
        break;
    default:
        unreachable("INVALID operand");
    }

    char *str = stringBuilderRelease(&sb);
    node->tag = astStringLit;
    node->stringLiteral.value = makeString(ctx->strPool, str);
    free(str);
}

static f64 getNumericLiteral(const AstNode *node)
{
    switch (node->tag) {
    case astBoolLit:
        return node->boolLiteral.value;
    case astCharLit:
        return node->charLiteral.value;
    case astIntegerLit:
        return node->intLiteral.hasMinus ? -node->intLiteral.value
                                         : node->intLiteral.value;
    case astFloatLit:
        return node->floatLiteral.value;
    default:
        unreachable("NOT A LITERAL");
    }
}

void setNumericLiteralValue(AstNode *node,
                            AstNode *lhs,
                            AstNode *rhs,
                            f64 value)
{
    switch (lhs->tag) {
    case astBoolLit:
        if (nodeIs(rhs, BoolLit)) {
            node->tag = astBoolLit;
            node->boolLiteral.value = value == 0;
        }
        else if (nodeIs(rhs, CharLit) || nodeIs(rhs, IntegerLit)) {
            node->tag = astIntegerLit;
            node->intLiteral.value = (i64)value;
            node->intLiteral.hasMinus = value < 0;
        }
        else {
            node->tag = astFloatLit;
            node->intLiteral.value = value;
        }
        break;
    case astCharLit:
        if (nodeIs(rhs, BoolLit) || nodeIs(rhs, CharLit)) {
            node->tag = astCharLit;
            node->charLiteral.value = (wchar)value;
        }
        else if (nodeIs(rhs, IntegerLit)) {
            node->tag = astIntegerLit;
            node->intLiteral.value = (i64)value;
            node->intLiteral.hasMinus = value < 0;
        }
        else {
            node->tag = astFloatLit;
            node->intLiteral.value = value;
        }
        break;
    case astIntegerLit:
        if (nodeIs(rhs, FloatLit)) {
            node->tag = astFloatLit;
            node->intLiteral.value = value;
        }
        else {
            node->tag = astIntegerLit;
            node->intLiteral.value = (i64)value;
            node->intLiteral.hasMinus = value < 0;
        }
        break;
    case astFloatLit:
        node->tag = astFloatLit;
        node->intLiteral.value = value;
        break;
    default:
        unreachable("NOT SUPPORTED");
    }
}

static void evalAddOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.lhs;
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

    f64 value = getNumericLiteral(lhs) + getNumericLiteral(rhs);
    setNumericLiteralValue(node, lhs, rhs, value);
}

static void evalSubOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.lhs;
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

    f64 value = getNumericLiteral(lhs) - getNumericLiteral(rhs);
    setNumericLiteralValue(node, lhs, rhs, value);
}

static void evalMulOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.lhs;
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

    f64 value = getNumericLiteral(lhs) * getNumericLiteral(rhs);
    setNumericLiteralValue(node, lhs, rhs, value);
}

static void evalDivOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.lhs;
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
            getNumericLiteral(lhs) / getNumericLiteral(rhs);
    }
    else {
        setNumericLiteralValue(
            node,
            lhs,
            rhs,
            ((i64)getNumericLiteral(lhs) / (i64)getNumericLiteral(rhs)));
    }
}

static void evalModOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.lhs;
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

    setNumericLiteralValue(
        node,
        lhs,
        rhs,
        ((i64)getNumericLiteral(lhs) % (i64)getNumericLiteral(rhs)));
}

static void evalShlOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.lhs;
    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `<<` cannot be of type string",
                 NULL);

        node->tag = astError;
        return;
    }

    if (nodeIs(lhs, FloatLit) || nodeIs(rhs, FloatLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `<<` cannot be float",
                 NULL);

        node->tag = astError;
        return;
    }

    if (nodeIs(lhs, BoolLit) || nodeIs(rhs, BoolLit)) {
        logWarning(ctx->L,
                   &node->loc,
                   "comp-time shift left with boolean operands doesn't "
                   "make sense",
                   NULL);
    }
    else if (nodeIs(lhs, CharLit) || nodeIs(rhs, CharLit)) {
        logWarning(ctx->L,
                   &node->loc,
                   "comp-time shift left with character operands doesn't "
                   "make sense",
                   NULL);
    }

    setNumericLiteralValue(
        node,
        lhs,
        rhs,
        ((i64)getNumericLiteral(lhs) << (i64)getNumericLiteral(rhs)));
}

static void evalShrOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.lhs;
    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `>>` cannot be of type string",
                 NULL);

        node->tag = astError;
        return;
    }

    if (nodeIs(lhs, FloatLit) || nodeIs(rhs, FloatLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `>>` cannot be float",
                 NULL);

        node->tag = astError;
        return;
    }

    if (nodeIs(lhs, BoolLit) || nodeIs(rhs, BoolLit)) {
        logWarning(ctx->L,
                   &node->loc,
                   "comp-time shift right with boolean operands doesn't "
                   "make sense",
                   NULL);
    }
    else if (nodeIs(lhs, CharLit) || nodeIs(rhs, CharLit)) {
        logWarning(ctx->L,
                   &node->loc,
                   "comp-time shift right with character operands doesn't "
                   "make sense",
                   NULL);
    }

    setNumericLiteralValue(
        node,
        lhs,
        rhs,
        ((i64)getNumericLiteral(lhs) << (i64)getNumericLiteral(rhs)));
}

static void evalBAndOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.lhs;
    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `&` cannot be of type string",
                 NULL);

        node->tag = astError;
        return;
    }

    if (nodeIs(lhs, FloatLit) || nodeIs(rhs, FloatLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `&` cannot be float",
                 NULL);

        node->tag = astError;
        return;
    }

    if (nodeIs(lhs, BoolLit) || nodeIs(rhs, BoolLit)) {
        logWarning(ctx->L,
                   &node->loc,
                   "comp-time binary and with boolean operands doesn't "
                   "make sense",
                   NULL);
    }
    else if (nodeIs(lhs, CharLit) || nodeIs(rhs, CharLit)) {
        logWarning(ctx->L,
                   &node->loc,
                   "comp-time binary and with character operands doesn't "
                   "make sense",
                   NULL);
    }

    setNumericLiteralValue(
        node,
        lhs,
        rhs,
        ((i64)getNumericLiteral(lhs) & (i64)getNumericLiteral(rhs)));
}

static void evalBOrOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.lhs;
    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `|` cannot be of type string",
                 NULL);

        node->tag = astError;
        return;
    }

    if (nodeIs(lhs, FloatLit) || nodeIs(rhs, FloatLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `|` cannot be float",
                 NULL);

        node->tag = astError;
        return;
    }

    if (nodeIs(lhs, BoolLit) || nodeIs(rhs, BoolLit)) {
        logWarning(ctx->L,
                   &node->loc,
                   "comp-time binary or with boolean operands doesn't "
                   "make sense",
                   NULL);
    }
    else if (nodeIs(lhs, CharLit) || nodeIs(rhs, CharLit)) {
        logWarning(ctx->L,
                   &node->loc,
                   "comp-time binary or with character operands doesn't "
                   "make sense",
                   NULL);
    }

    setNumericLiteralValue(
        node,
        lhs,
        rhs,
        ((i64)getNumericLiteral(lhs) | (i64)getNumericLiteral(rhs)));
}

static void evalBXorOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.lhs;
    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `^` cannot be of type string",
                 NULL);

        node->tag = astError;
        return;
    }

    if (nodeIs(lhs, FloatLit) || nodeIs(rhs, FloatLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `^` cannot be float",
                 NULL);

        node->tag = astError;
        return;
    }

    if (nodeIs(lhs, BoolLit) || nodeIs(rhs, BoolLit)) {
        logWarning(ctx->L,
                   &node->loc,
                   "comp-time binary xor with boolean operands doesn't "
                   "make sense",
                   NULL);
    }
    else if (nodeIs(lhs, CharLit) || nodeIs(rhs, CharLit)) {
        logWarning(ctx->L,
                   &node->loc,
                   "comp-time binary xor with character operands doesn't "
                   "make sense",
                   NULL);
    }

    setNumericLiteralValue(
        node,
        lhs,
        rhs,
        ((i64)getNumericLiteral(lhs) ^ (i64)getNumericLiteral(rhs)));
}

static void evalLAndOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.lhs;
    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `&&` cannot be of type string",
                 NULL);

        node->tag = astError;
        return;
    }

    node->tag = astBoolLit;
    node->boolLiteral.value = getNumericLiteral(lhs) && getNumericLiteral(rhs);
}

static void evalLOrOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.lhs;
    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `&&` cannot be of type string",
                 NULL);

        node->tag = astError;
        return;
    }

    node->tag = astBoolLit;
    node->boolLiteral.value = getNumericLiteral(lhs) || getNumericLiteral(rhs);
}

static void evalEqOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.lhs;
    if (nodeIs(lhs, StringLit) && nodeIs(rhs, StringLit)) {
        memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);
        node->tag = astBoolLit;
        node->boolLiteral.value =
            lhs->stringLiteral.value == rhs->stringLiteral.value;
        return;
    }

    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "unsupported comp-time operation '==' between a string and a "
                 "non-string literal",
                 NULL);

        node->tag = astError;
        return;
    }

    memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);
    node->tag = astBoolLit;
    node->boolLiteral.value = getNumericLiteral(lhs) == getNumericLiteral(rhs);
}

static void evalNeOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.lhs;
    if (nodeIs(lhs, StringLit) && nodeIs(rhs, StringLit)) {
        memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);
        node->tag = astBoolLit;
        node->boolLiteral.value =
            lhs->stringLiteral.value != rhs->stringLiteral.value;
        return;
    }

    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "unsupported comp-time operation '!=' between a string and a "
                 "non-string literal",
                 NULL);

        node->tag = astError;
        return;
    }

    memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);
    node->tag = astBoolLit;
    node->boolLiteral.value = getNumericLiteral(lhs) != getNumericLiteral(rhs);
}

static void evalLtOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.lhs;
    if (nodeIs(lhs, StringLit) && nodeIs(rhs, StringLit)) {
        memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);
        node->tag = astBoolLit;
        node->boolLiteral.value =
            strcmp(lhs->stringLiteral.value, rhs->stringLiteral.value) < 0;
        return;
    }

    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "unsupported comp-time operation '<' between a string and a "
                 "non-string literal",
                 NULL);

        node->tag = astError;
        return;
    }

    memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);
    node->tag = astBoolLit;
    node->boolLiteral.value = getNumericLiteral(lhs) < getNumericLiteral(rhs);
}

static void evalGtOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.lhs;
    if (nodeIs(lhs, StringLit) && nodeIs(rhs, StringLit)) {
        memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);
        node->tag = astBoolLit;
        node->boolLiteral.value =
            strcmp(lhs->stringLiteral.value, rhs->stringLiteral.value) > 0;
        return;
    }

    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "unsupported comp-time operation '>' between a string and a "
                 "non-string literal",
                 NULL);

        node->tag = astError;
        return;
    }

    memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);
    node->tag = astBoolLit;
    node->boolLiteral.value = getNumericLiteral(lhs) < getNumericLiteral(rhs);
}

static void evalGeqOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.lhs;
    if (nodeIs(lhs, StringLit) && nodeIs(rhs, StringLit)) {
        memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);
        node->tag = astBoolLit;
        node->boolLiteral.value =
            strcmp(lhs->stringLiteral.value, rhs->stringLiteral.value) >= 0;
        return;
    }

    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "unsupported comp-time operation '>=' between a string and a "
                 "non-string literal",
                 NULL);

        node->tag = astError;
        return;
    }

    memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);
    node->tag = astBoolLit;
    node->boolLiteral.value = getNumericLiteral(lhs) >= getNumericLiteral(rhs);
}

static void evalLeqOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.lhs;
    if (nodeIs(lhs, StringLit) && nodeIs(rhs, StringLit)) {
        memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);
        node->tag = astBoolLit;
        node->boolLiteral.value =
            strcmp(lhs->stringLiteral.value, rhs->stringLiteral.value) <= 0;
        return;
    }

    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "unsupported comp-time operation '<=' between a string and a "
                 "non-string literal",
                 NULL);

        node->tag = astError;
        return;
    }

    memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);
    node->tag = astBoolLit;
    node->boolLiteral.value = getNumericLiteral(lhs) <= getNumericLiteral(rhs);
}

static void evalRangeOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.lhs;
    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "unsupported comp-time operation '<=' between a string and a "
                 "non-string literal",
                 NULL);

        node->tag = astError;
        return;
    }
    unreachable("TODO");
}

static void evalPrefixMinusOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *rhs = node->unaryExpr.operand;
    if (nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operand of prefix `-` cannot be of type string",
                 NULL);

        node->tag = astError;
        return;
    }

    if (nodeIs(rhs, BoolLit)) {
        logWarning(
            ctx->L,
            &node->loc,
            "comp-time prefix `-` operation with boolean operands doesn't "
            "make sense",
            NULL);
    }
    else if (nodeIs(rhs, CharLit)) {
        logWarning(
            ctx->L,
            &node->loc,
            "comp-time prefix `-` operation with character operands doesn't "
            "make sense",
            NULL);
    }

    f64 value = -getNumericLiteral(rhs);
    if (nodeIs(node, FloatLit)) {
        node->tag = astFloatLit;
        node->floatLiteral.value = value;
    }
    else {
        node->tag = astIntegerLit;
        node->intLiteral.value = value;
    }
}

static void evalPrefixPlusOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *rhs = node->unaryExpr.operand;
    if (nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operand of prefix `+` cannot be of type string",
                 NULL);

        node->tag = astError;
        return;
    }

    if (nodeIs(rhs, BoolLit)) {
        logWarning(
            ctx->L,
            &node->loc,
            "comp-time prefix `+` operation with boolean operands doesn't "
            "make sense",
            NULL);
    }
    else if (nodeIs(rhs, CharLit)) {
        logWarning(
            ctx->L,
            &node->loc,
            "comp-time prefix `+` operation with character operands doesn't "
            "make sense",
            NULL);
    }

    f64 value = +getNumericLiteral(rhs);
    if (nodeIs(node, FloatLit)) {
        node->tag = astFloatLit;
        node->floatLiteral.value = value;
    }
    else {
        node->tag = astIntegerLit;
        node->intLiteral.value = value;
    }
}

static void evalLogicNotOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *rhs = node->unaryExpr.operand;
    if (nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operand of prefix `!` cannot be of type string",
                 NULL);

        node->tag = astError;
        return;
    }

    node->tag = astBoolLit;
    node->boolLiteral.value = !getNumericLiteral(rhs);
}

static void evalBitNotOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *rhs = node->binaryExpr.lhs;
    if (nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `~` cannot be of type string",
                 NULL);

        node->tag = astError;
        return;
    }

    if (nodeIs(rhs, FloatLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `~` cannot be float",
                 NULL);

        node->tag = astError;
        return;
    }

    if (nodeIs(rhs, BoolLit)) {
        logWarning(ctx->L,
                   &node->loc,
                   "comp-time binary bit not with boolean operands doesn't "
                   "make sense",
                   NULL);
    }
    else if (nodeIs(rhs, CharLit)) {
        logWarning(ctx->L,
                   &node->loc,
                   "comp-time binary bit not with character operands doesn't "
                   "make sense",
                   NULL);
    }

    node->tag = astIntegerLit;
    node->intLiteral.value = ~((i64)getNumericLiteral(rhs));
}

static bool evalStringIndexExpr(SemanticsContext *ctx, AstNode *node)
{
    AstNode *target = node->indexExpr.target;
    AstNode *index = node->indexExpr.index;

    if (!nodeIs(target, EnumDecl) && !nodeIs(target, StructDecl)) {
        logError(ctx->L,
                 &target->loc,
                 "comp-time string index operator only supported on enum or "
                 "struct declaration typeinfo instances",
                 NULL);
        node->tag = astError;
        return false;
    }

    AstNode *member =
        nodeIs(target, EnumDecl)
            ? findEnumOptionByName(target, index->stringLiteral.value)
            : findStructMemberByName(target, index->stringLiteral.value);

    if (member == NULL)
        node->tag = astNullLit;
    else
        *node = *member;
    return true;
}

static bool evalIntegerIndexExpr(SemanticsContext *ctx, AstNode *node)
{
    AstNode *target = node->indexExpr.target;
    AstNode *index = node->indexExpr.index;

    if (!nodeIs(target, ArrayExpr) && !nodeIs(target, StringLit)) {
        logError(ctx->L,
                 &target->loc,
                 "comp-time integer index operator only supported on string or "
                 "array "
                 "expressions",
                 NULL);
        node->tag = astError;
        return false;
    }

    i64 i = getNumericLiteral(index);
    u64 len = nodeIs(target, StringLit) ? strlen(target->stringLiteral.value)
                                        : target->arrayExpr.len;
    if (i < 0 || i >= len) {
        logError(ctx->L,
                 &node->loc,
                 "index out of bounds for comp-time index "
                 "expression, requested index '{i64}', expecting '< {u64'}",
                 (FormatArg[]){{.i64 = i}, {.u64 = len}});

        node->tag = astError;
        return false;
    }

    if (nodeIs(target, StringLit)) {
        memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);
        node->tag = astCharLit;
        node->charLiteral.value = target->stringLiteral.value[i];
    }
    else {
        *node = *getNodeAtIndex(target->arrayExpr.elements, i);
    }

    return true;
}

static bool evalIntegerMemberExpr(SemanticsContext *ctx, AstNode *node)
{
    AstNode *target = node->memberExpr.target;
    AstNode *member = node->memberExpr.member;

    if (!nodeIs(target, TupleExpr) && !nodeIs(target, TupleType)) {
        logError(ctx->L,
                 &target->loc,
                 "comp-time member expression operator only supported on tuple "
                 "expressions or type declarations",
                 NULL);
        node->tag = astError;
        return false;
    }

    i64 i = getNumericLiteral(member);
    u64 len = target->tupleExpr.len;
    if (i < 0 || i >= len) {
        logError(ctx->L,
                 &node->loc,
                 "member out of bounds for comp-time integer member "
                 "expression, requested index '{i64}', expecting '< {u64'}",
                 (FormatArg[]){{.i64 = i}, {.u64 = len}});

        node->tag = astError;
        return false;
    }

    *node = *getNodeAtIndex(target->tupleExpr.args, i);
    return true;
}

static bool evalStringMemberExpr(SemanticsContext *ctx, AstNode *node)
{
    AstNode *target = node->memberExpr.target;
    AstNode *member = node->memberExpr.member;

    if (!nodeIs(target, EnumDecl)) {
        logError(ctx->L,
                 &target->loc,
                 "comp-time member expression operator only supported on enum "
                 "types",
                 NULL);
        node->tag = astError;
        return false;
    }

    AstNode *value = findEnumOptionByName(target, member->stringLiteral.value);
    if (value == NULL)
        node->tag = astNullLit;
    else
        *node = *value->enumOption.value;
    return true;
}

void evalIndexExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *target = node->indexExpr.target;

    if (!evaluate(visitor, target)) {
        node->tag = astError;
        return;
    }

    AstNode *index = node->indexExpr.index;
    if (!evaluate(visitor, index)) {
        node->tag = astError;
        return;
    }

    if (nodeIs(index, StringLit)) {
        evalStringIndexExpr(ctx, node);
    }
    else if (nodeIs(index, IntegerLit)) {
        evalIntegerIndexExpr(ctx, node);
    }
    else {
        logError(ctx->L,
                 &node->loc,
                 "unexpected comp-time index expression, index can either be a "
                 "string or integer literal",
                 NULL);
        node->tag = astError;
    }
}

void evalMemberExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *target = node->memberExpr.target;

    if (!evaluate(visitor, target)) {
        node->tag = astError;
        return;
    }

    AstNode *member = node->memberExpr.member;
    if (!evaluate(visitor, member)) {
        node->tag = astError;
        return;
    };

    if (nodeIs(member, IntegerLit)) {
        evalIntegerMemberExpr(ctx, node);
    }
    else if (nodeIs(member, Identifier)) {
        evalStringMemberExpr(ctx, node);
    }
    else {
        logError(
            ctx->L,
            &node->loc,
            "unexpected comp-time member expression, target can either be a "
            "tuple expression or an enum type",
            NULL);
        node->tag = astError;
    }
}

bool evalBooleanCast(SemanticsContext *ctx, AstNode *node)
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
        node->boolLiteral.value = node->intLiteral.value != 0;
        node->tag = astBoolLit;
        break;
    case astFloatLit:
        node->boolLiteral.value = node->floatLiteral.value != 0;
        node->tag = astBoolLit;
        break;
    case astStringLit:
        node->boolLiteral.value == (node->stringLiteral.value != NULL) &&
            (node->stringLiteral.value[0] != '\0');
        node->tag = astBoolLit;
        break;
    default:
        logError(ctx->L,
                 &node->loc,
                 "comp-time expression cannot be converted to boolean",
                 NULL);
        node->tag = astError;
        return false;
    }

    return true;
}

void evalIfExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    AstNode *cond = node->ifStmt.cond;
    if (!evaluate(visitor, cond) || !evalBooleanCast(ctx, node)) {
        node->tag = astError;
        return;
    }

    if (cond->boolLiteral.value) {
        // select then branch & reclaim else branch if any
        *node = *node->ifStmt.cond;
    }
    else if (node->ifStmt.otherwise) {
        // select otherwise, reclaim if branch
        *node = *node->ifStmt.otherwise;
    }
    else {
        // select next statement, reclaim if branch
        if (node->next)
            *node = *node->next;
        else {
            memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);
            node->tag = astBoolLit;
        }
    }
}

void evalBinaryExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    if (!verifyLiteralExpressions(ctx, node))
        return;

    switch (node->binaryExpr.op) {
#define f(O, ...)                                                              \
    case op##O:                                                                \
        eval##O##Operation(ctx, node);                                         \
        break;
        AST_BINARY_EXPR_LIST(f)
    default:
        unreachable("NOT supported binary operator")
    }
}

bool evaluate(AstVisitor *visitor, AstNode *node)
{
    astVisit(visitor, node);
    return node->tag != astError;
}