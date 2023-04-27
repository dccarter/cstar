//
// Created by Carter on 2023-04-26.
//

#include "lang/semantics.h"

#include "lang/native/setup.h"
#include "lang/ttable.h"

#include <string.h>

static bool verifyLiteralExpressions(SemanticsContext *ctx,
                                     AstNode *node,
                                     AstNode *lhs,
                                     AstNode *rhs,
                                     cstring op)
{
    if (!isLiteralExpr(lhs)) {
        logError(ctx->L,
                 &lhs->loc,
                 "operand of `{s}` must be a literal at compile time",
                 (FormatArg[]){{.s = op}});
        node->tag = astError;
        return false;
    }

    if (!isLiteralExpr(rhs)) {
        logError(ctx->L,
                 &rhs->loc,
                 "operand of `{s}` must be a literal at compile time",
                 (FormatArg[]){{.s = op}});
        node->tag = astError;
        return false;
    }

    return true;
}

static bool evalStringAdd(SemanticsContext *ctx,
                          AstNode *node,
                          AstNode *lhs,
                          AstNode *rhs)
{
    cxy_string_builder_t sb;
    cxy_string_builder_init(&sb);
    cxy_string_builder_append_cstr1(&sb, lhs->stringLiteral.value);
    switch (rhs->tag) {
    case astStringLit:
        cxy_string_builder_append_cstr1(&sb, rhs->stringLiteral.value);
        break;
    case astIntegerLit:
        if (rhs->intLiteral.hasMinus)
            cxy_string_builder_append_char(&sb, '-');
        cxy_string_builder_append_int(&sb, rhs->intLiteral.value);
        break;
    case astFloatLit:
        cxy_string_builder_append_float(&sb, rhs->floatLiteral.value);
        break;
    case astCharLit:
        cxy_string_builder_append_char(&sb, rhs->charLiteral.value);
        break;
    case astBoolLit:
        cxy_string_builder_append_bool(&sb, rhs->boolLiteral.value);
        break;
    default:
        unreachable("INVALID operand");
    }

    node->tag = astStringLit;
    node->stringLiteral.value = cxy_string_builder_release(&sb);
}

#define floatOrInteger(N)                                                      \
    (nodeIs((N), FloatLit) ? (N)->floatLiteral.value                           \
                           : (f64)(N)->intLiteral.value)

static f64 getNumericLiteral(const AstNode *node)
{
    switch (node->tag) {
    case astIntegerLit:
        return node->intLiteral.hasMinus ? -node->intLiteral.value
                                         : node->intLiteral.value;
    case astCharLit:
        return node->charLiteral.value;
    case astFloatLit:
        return node->floatLiteral.value;

    default:
        unreachable("Not a numeric literal");
    }
}

void evalBinaryOpAdd(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs;
    AstNode *rhs = node->binaryExpr.rhs;
    cstring op = getBinaryOpString(node->binaryExpr.op);

    if (!verifyLiteralExpressions(ctx, node, lhs, rhs, op))
        return;

    if (nodeIs(rhs, StringLit) && nodeIs(lhs, StringLit)) {
        logError(ctx->L,
                 &lhs->loc,
                 "operand of `+` must be a literal at compile time",
                 NULL);
        node->tag = astError;
        return;
    }

    memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);
    switch (lhs->tag) {
    case astStringLit:
        evalStringAdd(ctx, node, lhs, rhs);
        break;
    case astIntegerLit:
        if (!(nodeIs(lhs, FloatLit) || nodeIs(rhs, FloatLit))) {
            f64 value = getNumericLiteral(lhs) + getNumericLiteral(rhs);
            node->tag = astIntegerLit;
            node->intLiteral.hasMinus = value < 0;
            node->intLiteral.value = (i64)value;
            break;
        }
        // fallthrough
    case astFloatLit:
        node->tag = astFloatLit;
        node->floatLiteral.value =
            getNumericLiteral(lhs) + getNumericLiteral(rhs);
        break;

    case astCharLit:
        if (nodeIs(rhs, FloatLit)) {
            node->tag = astFloatLit;
            node->floatLiteral.value =
                getNumericLiteral(lhs) + getNumericLiteral(rhs);
        }
        else if (nodeIs(rhs, IntegerLit)) {
            i64 value = lhs->charLiteral.value + rhs->intLiteral.value;
            node->tag = astCharLit;
            node->charLiteral.value = (wchar)(value);
        }
        break;
    default:
        unreachable("unsupported type!");
    }
}

void evalBinaryOpSubtract(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs;
    AstNode *rhs = node->binaryExpr.rhs;
    cstring op = getBinaryOpString(node->binaryExpr.op);

    if (!verifyLiteralExpressions(ctx, node, lhs, rhs, op))
        return;

    if (nodeIs(rhs, StringLit) || nodeIs(lhs, StringLit)) {
        logError(ctx->L,
                 &lhs->loc,
                 "operation `-` cannot be performed on strings",
                 NULL);
        node->tag = astError;
        return;
    }

    memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);
    switch (lhs->tag) {
    case astIntegerLit:
        if (!(nodeIs(lhs, FloatLit) || nodeIs(rhs, FloatLit))) {
            f64 value = getNumericLiteral(lhs) - getNumericLiteral(rhs);
            node->tag = astIntegerLit;
            node->intLiteral.hasMinus = value < 0;
            node->intLiteral.value = (i64)value;
            break;
        }
        // fallthrough
    case astFloatLit:
        node->tag = astFloatLit;
        node->floatLiteral.value =
            getNumericLiteral(lhs) - getNumericLiteral(rhs);
        break;

    case astCharLit:
        if (nodeIs(rhs, FloatLit)) {
            logError(ctx->L,
                     &lhs->loc,
                     "operation `-` cannot be performed on between a character "
                     "and a float",
                     NULL);
            node->tag = astError;
            return;
        }
        else if (nodeIs(rhs, IntegerLit)) {
            i64 value = lhs->charLiteral.value + rhs->intLiteral.value;
            node->tag = astCharLit;
            node->charLiteral.value = (wchar)(value);
        }
        else {
            i64 value = lhs->charLiteral.value + rhs->intLiteral.value;
            node->tag = astCharLit;
            node->charLiteral.value = (wchar)(value);
        }
        break;
    default:
        unreachable("unsupported type!");
    }
}