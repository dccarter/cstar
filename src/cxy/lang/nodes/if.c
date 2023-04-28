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
#include "lang/eval.h"
#include "lang/semantics.h"

#include "lang/ttable.h"

void generateIfStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *cond = node->ifStmt.cond;
    if (cond->tag == astVarDecl) {
        format(ctx->state, "{{{>}\n", NULL);
        astConstVisit(visitor, cond);
        format(ctx->state,
               "\nif ({s}) ",
               (FormatArg[]){{.s = cond->varDecl.names->ident.value}});
    }
    else {
        format(ctx->state, "if (", NULL);
        astConstVisit(visitor, cond);
        format(ctx->state, ") ", NULL);
    }
    astConstVisit(visitor, node->ifStmt.body);
    if (node->ifStmt.otherwise) {
        format(ctx->state, " else ", NULL);
        astConstVisit(visitor, node->ifStmt.otherwise);
    }

    if (cond->tag == astVarDecl) {
        format(ctx->state, "{<}\n}", NULL);
    }
}

void checkIfStmt(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    pushScope(ctx->env, node);

    const Type *cond = evalType(visitor, node->ifStmt.cond);
    const Type *then = evalType(visitor, node->ifStmt.body);

    if (!isTypeAssignableFrom(getPrimitiveType(ctx->typeTable, prtBool),
                              cond)) {
        logError(ctx->L,
                 &node->ternaryExpr.cond->loc,
                 "unexpected type in if statement condition, expecting "
                 "a truthy expression but got '{t}'",
                 (FormatArg[]){{.t = cond}});
        node->type = ERROR_TYPE(ctx);
    }
    else {
        node->type = then;
    }

    if (node->ifStmt.otherwise) {
        evalType(visitor, node->ifStmt.otherwise);
    }

    popScope(ctx->env);
}

void evalIfStmt(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    AstNode *cond = node->ifStmt.cond;
    if (!evaluate(visitor, cond) || !evalBooleanCast(ctx, cond)) {
        node->tag = astError;
        return;
    }

    AstNode *next = node->next;
    AstNode *parent = node->parentScope;

    if (cond->boolLiteral.value) {
        // select then branch & reclaim else branch if any
        *node = *node->ifStmt.body;
        node->next = next;
        node->parentScope = parent;
    }
    else if (node->ifStmt.otherwise) {
        // select otherwise, reclaim if branch
        *node = *node->ifStmt.otherwise;
        node->next = next;
        node->parentScope = parent;
    }
    else {
        // select next statement, reclaim if branch
        if (node->next)
            *node = *node->next;
        else {
            clearAstBody(node);
            node->tag = astNop;
            node->flags &= ~flgComptime;
        }
    }
}
