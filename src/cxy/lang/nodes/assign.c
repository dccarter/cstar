/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-19
 */

#include "lang/codegen.h"
#include "lang/semantics.h"

#include "lang/eval.h"
#include "lang/flag.h"
#include "lang/strings.h"
#include "lang/ttable.h"
#include "lang/visitor.h"

#include <memory.h>

static void checkIndexExprAssignment(AstVisitor *visitor, AstNode *node)
{
    // change X.[a] = y to X.op_idx_assign(a, y);
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *left = node->assignExpr.lhs;
    const Type *lhs = left->indexExpr.target->type;
    const Type *target = stripPointer(lhs);

    AstNode *func =
        findSymbolOnly(target->tStruct.decl->env, S_IndexAssignOverload);
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

    transformToMemberCallExpr(visitor,
                              node,
                              func,
                              left->indexExpr.target,
                              S_IndexAssignOverload,
                              args);
    evalType(visitor, node);
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
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *left = node->assignExpr.lhs, *right = node->assignExpr.rhs;

    if (nodeIs(left, IndexExpr)) {
        const Type *target = evalType(visitor, left->indexExpr.target);
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

    const Type *lhs = evalType(visitor, left);
    if (hasFlag(left, AddThis)) {
        AstNode *func = findEnclosingFunc(ctx->env, NULL, NULL);
        left->flags |= (func->flags & flgConst);
    }

    const Type *rhs = evalType(visitor, right);
    bool isLeftAuto = typeIs(lhs, Auto);

    // TODO check r-value-ness
    if (hasFlag(left, Const) || hasFlag(lhs, Const)) {
        logError(ctx->L,
                 &node->loc,
                 "lhs of assignment expression is a constant",
                 (FormatArg[]){{.t = lhs}});
        node->type = ERROR_TYPE(ctx);
    }
    else if (isArrayType(rhs) && !isSliceType(rhs)) {
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
        const char *variable = left->path.elements->pathElement.name;
        AstNode *symbol = findSymbol(ctx->env, ctx->L, variable, &left->loc);
        csAssert0(symbol);
        symbol->type = rhs;
        node->type = symbol->type;
    }
    else {
        node->type = lhs;
    }
}

void evalAssignExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *left = node->assignExpr.lhs, *right = node->assignExpr.rhs;
    environmentPop(ctx->eval.env);
    SymbolRef *symbol = findSymbolRefByNode(ctx->eval.env, ctx->L, left);
    environmentPush(ctx->env, ctx->eval.env);

    if (symbol == NULL) {
        node->tag = astError;
        return;
    }

    if (!evaluate(visitor, right)) {
        node->tag = astError;
        return;
    }

    node->tag = astNop;
    if (node->assignExpr.op == opAssign) {
        symbol->node = right;
        return;
    }

    AstNode lhs = *symbol->node;
    AstNode binary = (AstNode){
        .tag = astBinaryExpr,
        .loc = node->loc,
        .binaryExpr = {.op = node->assignExpr.op, .lhs = &lhs, .rhs = right}};

    evalBinaryExpr(visitor, &binary);
    *symbol->node = binary;
}
