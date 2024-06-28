//
// Created by Carter Mbotho on 2024-01-09.
//

#include "eval.h"

#include "lang/frontend/flag.h"

static bool evalIntegerMemberExpr(EvalContext *ctx, AstNode *node)
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

    i64 i = (i64)nodeGetNumericLiteral(member);
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

    *node = *getNodeAtIndex(target->tupleExpr.elements, i);
    return true;
}

static bool evalStringMemberExpr(EvalContext *ctx, AstNode *node)
{
    AstNode *target = node->memberExpr.target;
    AstNode *member = node->memberExpr.member;
    target = nodeIs(target, GroupExpr) ? target->groupExpr.expr : target;

    if (nodeIs(target, EnumDecl)) {
        AstNode *value =
            findEnumOptionByName(target, member->stringLiteral.value);
        if (value == NULL)
            node->tag = astNullLit;
        else
            replaceAstNode(node, value->enumOption.value);
        return true;
    }

    logError(ctx->L,
             &target->loc,
             "comp-time member expression operator only supported on enum "
             "types",
             NULL);
    node->tag = astError;
    return false;
}

void evalMemberExpr(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    AstNode *target = node->memberExpr.target;

    if (!evaluate(visitor, target)) {
        node->tag = astError;
        return;
    }

    AstNode *member = node->memberExpr.member;
    if (nodeIs(member, IntegerLit)) {
        evalIntegerMemberExpr(ctx, node);
    }
    else if (nodeIs(member, StringLit)) {
        evalStringMemberExpr(ctx, node);
    }
    else if (nodeIs(member, Identifier)) {
        AstNode *value = evalAstNodeMemberAccess(
            ctx,
            &node->loc,
            nodeIs(target, Ref) ? target->reference.target : target,
            member->ident.value);
        if (value == NULL) {
            node->tag = astError;
            return;
        }

        replaceAstNode(node, value);
    }
    else if (nodeIs(member, Path)) {
        evalPathEpilogue(visitor,
                         node,
                         nodeIs(target, Ref) ? target->reference.target
                                             : target,
                         member->path.elements);
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
