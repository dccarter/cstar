//
// Created by Carter Mbotho on 2024-01-09.
//

#include "eval.h"
#include <lang/frontend/flag.h>

static bool evalAstNodeAtIndex(EvalContext *ctx,
                               AstNode *node,
                               AstNode *list,
                               i64 index)
{
    AstNode *atIndex = getNodeAtIndex(list, index);
    if (atIndex == NULL) {
        logError(ctx->L,
                 &node->loc,
                 "index {0} is out of bounds, target has {u64} elements",
                 (FormatArg[]){{.i64 = index}, {.u64 = countAstNodes(list)}});
        node->tag = astError;
        return false;
    }
    *node = *atIndex;
    return true;
}

bool setEvalStringIndexExprResult(AstNode *node, const AstNode *result)
{
    if (result) {
        *node = *result;
        return false;
    }
    node->tag = astNullLit;
    return false;
}

static bool evalStringIndexExpr(EvalContext *ctx, AstNode *node)
{
    AstNode *target = node->indexExpr.target;
    AstNode *index = node->indexExpr.index;

    switch (target->tag) {
    case astEnumDecl:
        return setEvalStringIndexExprResult(
            node, findEnumOptionByName(target, index->stringLiteral.value));
    case astStructDecl:
    case astClassDecl:
    case astInterfaceDecl:
        return setEvalStringIndexExprResult(
            node, findInAstNode(target, index->stringLiteral.value));
    case astComptimeOnly:
        return setEvalStringIndexExprResult(
            node, findInComptimeIterable(target, index->stringLiteral.value));
    case astAttr:
        if (target->attr.kvpArgs)
            return setEvalStringIndexExprResult(
                node,
                findAttributeArgument(target, index->stringLiteral.value));
    case astNullLit:
        node->tag = astNullLit;
        return false;
    default:
        break;
    }

    logError(
        ctx->L,
        &target->loc,
        "comptime string index expression is not supported on given target",
        NULL);
    node->tag = astError;
    return false;
}

static bool evalIntegerIndexExpr(EvalContext *ctx, AstNode *node)
{
    AstNode *target = node->indexExpr.target;
    AstNode *index = node->indexExpr.index;

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

    switch (target->tag) {
    case astStringLit:
        if (i > strlen(target->stringLiteral.value)) {
            logError(
                ctx->L,
                &node->loc,
                "index {i64} is out of range for string {s}",
                (FormatArg[]){{.i64 = i}, {.s = target->stringLiteral.value}});
            return false;
        }
        clearAstBody(node);
        node->tag = astCharLit;
        node->charLiteral.value = (wchar)target->stringLiteral.value[i];
        return true;
    case astArrayExpr:
        return evalAstNodeAtIndex(ctx, node, target->arrayExpr.elements, i);
    case astTupleExpr:
        return evalAstNodeAtIndex(ctx, node, target->tupleExpr.elements, i);
    case astAttr:
        if (target->attr.kvpArgs) {
            logError(
                ctx->L,
                &node->loc,
                "attribute {s} argument were given by name, use named index "
                "expression",
                (FormatArg[]){{.s = target->attr.name}});
            node->tag = astError;
            return false;
        }
        return evalAstNodeAtIndex(ctx, node, target->attr.args, i);
    case astComptimeOnly:
        if (hasFlag(target, ComptimeIterable))
            return evalAstNodeAtIndex(ctx, node, target->next, i);
        break;
    default:
        break;
    }

    logError(ctx->L,
             &target->loc,
             "comp-time integer index operator is not supported on current "
             "expression",
             NULL);
    node->tag = astError;
    return false;
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
