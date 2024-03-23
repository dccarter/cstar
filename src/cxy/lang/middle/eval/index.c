//
// Created by Carter Mbotho on 2024-01-09.
//

#include "eval.h"

static bool evalStringIndexExpr(EvalContext *ctx, AstNode *node)
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
            : findMemberByName(target, index->stringLiteral.value);

    if (member == NULL)
        node->tag = astNullLit;
    else
        *node = *member;
    return true;
}

static bool evalIntegerIndexExpr(EvalContext *ctx, AstNode *node)
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

    i64 i = (i64)nodeGetNumericLiteral(index);
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
        clearAstBody(node);
        node->tag = astCharLit;
        node->charLiteral.value = (wchar)target->stringLiteral.value[i];
    }
    else {
        *node = *getNodeAtIndex(target->arrayExpr.elements, i);
    }

    return true;
}

void evalIndexExpr(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
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
