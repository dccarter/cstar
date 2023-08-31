/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-19
 */

#include "../check.h"
#include "../codegen.h"

#include "lang/flag.h"
#include "lang/operations.h"
#include "lang/strings.h"
#include "lang/ttable.h"

static void checkIndexExprAssignment(AstVisitor *visitor, AstNode *node)
{
    // change X.[a] = y to X.op_idx_assign(a, y);
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *left = node->assignExpr.lhs;
    const Type *lhs = left->indexExpr.target->type;
    const Type *target = stripPointer(lhs);

    const Type *func = findStructMemberType(lhs, S_IndexAssignOverload);
    if (func == NULL) {
        logError(ctx->L,
                 &node->assignExpr.rhs->loc,
                 "cannot assign to struct index expression, type '{t}' does "
                 "not overload index assignment `[]=` operator",
                 (FormatArg[]){{.t = target}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    AstNode *args = left->indexExpr.index;
    args->next = node->assignExpr.rhs;

    transformToMemberCallExpr(
        visitor, node, left->indexExpr.target, S_IndexAssignOverload, args);
    checkType(visitor, node);
}

void generateAssignExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->assignExpr.lhs);
    format(ctx->state,
           " {s} ",
           (FormatArg[]){{.s = getAssignOpString(node->assignExpr.op)}});
    astConstVisit(visitor, node->assignExpr.rhs);
}

void checkAssignExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *left = node->assignExpr.lhs, *right = node->assignExpr.rhs;

    if (nodeIs(left, IndexExpr)) {
        const Type *target = checkType(visitor, left->indexExpr.target);
        target = stripPointer(target);
        if (typeIs(target, Struct)) {
            checkIndexExprAssignment(visitor, node);
            return;
        }

        if (typeIs(target, Error)) {
            node->type = target;
            return;
        }
    }

    const Type *lhs = checkType(visitor, left);
    if (typeIs(lhs, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }
    //    if (hasFlag(left, AddThis)) {
    //        AstNode *func = findEnclosingFunc(ctx->env, NULL, NULL);
    //        left->flags |= (func->flags & flgConst);
    //    }

    const Type *rhs = checkType(visitor, right);
    if (typeIs(rhs, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    bool isLeftAuto = typeIs(lhs, Auto);

    // TODO check r-value-ness
    if (hasFlag(left, Const) || hasFlag(lhs, Const)) {
        logError(ctx->L,
                 &node->loc,
                 "lhs of assignment expressions is a constant",
                 (FormatArg[]){{.t = lhs}});
        node->type = ERROR_TYPE(ctx);
    }
    else if (typeIs(rhs, Array) && rhs->array.len != UINT64_MAX) {
        if (isLeftAuto)
            logError(ctx->L,
                     &node->loc,
                     "array assignment not allowed, assignment should be done "
                     "at initialisation",
                     NULL);
        else
            logError(
                ctx->L, &node->loc, "assign to an array is not allowed", NULL);
        node->type = ERROR_TYPE(ctx);
    }
    else if (!isTypeAssignableFrom(lhs, rhs) &&
             !(typeIs(lhs, Pointer) && typeIs(rhs, Null))) {
        logError(ctx->L,
                 &node->assignExpr.rhs->loc,
                 "incompatible types on assigment expression, expecting '{t}', "
                 "got '{t}'",
                 (FormatArg[]){{.t = lhs}, {.t = rhs}});
        node->type = ERROR_TYPE(ctx);
    }
    if (node->type == ERROR_TYPE(ctx))
        return;

    if (isLeftAuto) {
        csAssert0(nodeIs(left, Path));
        AstNode *resolvesTo = left->path.elements->pathElement.resolvesTo;
        csAssert0(resolvesTo);
        resolvesTo->type = rhs;
        node->type = rhs;
    }
    else {
        node->type = lhs;
    }
}
