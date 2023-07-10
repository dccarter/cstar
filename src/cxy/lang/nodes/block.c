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

#include "lang/ttable.h"

void generateBlock(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *ret = NULL;
    const AstNode *epilogue = node->blockStmt.epilogue.first;

    format(ctx->state, "{{{>}\n", NULL);
    for (const AstNode *stmt = node->blockStmt.stmts; stmt; stmt = stmt->next) {
        if (epilogue && stmt->tag == astReturnStmt) {
            ret = stmt;
            continue;
        }
        astConstVisit(visitor, stmt);
        if (stmt->tag == astCallExpr)
            format(ctx->state, ";", NULL);
        if (epilogue || stmt->next)
            format(ctx->state, "\n", NULL);
    }

    for (; epilogue; epilogue = epilogue->next) {
        astConstVisit(visitor, epilogue);
        if ((epilogue->flags & flgDeferred) && epilogue->tag != astBlockStmt)
            format(ctx->state, ";", NULL);

        if (ret || epilogue->next)
            format(ctx->state, "\n", NULL);
    }

    if (ret)
        astConstVisit(visitor, ret);
    format(ctx->state, "{<}\n}", NULL);
}

typedef struct {
    AstNode *block;
    SemanticsContext *ctx;
} VisitSymbols;

AstNode *makeDelete(SemanticsContext *ctx,
                    const AstNode *node,
                    const Type *type)
{
    AstNode *target = makeAstNode(
        ctx->pool,
        &node->loc,
        &(AstNode){.tag = astIdentifier,
                   .flags = node->flags,
                   .type = type,
                   .ident.value = node->varDecl.names->ident.value});

    if (!typeIs(type, Pointer)) {
        target = makeAstNode(ctx->pool,
                             &node->loc,
                             &(AstNode){.tag = astAddressOf,
                                        .flags = node->flags,
                                        .type = type,
                                        .unaryExpr = {.isPrefix = true,
                                                      .op = opAddrOf,
                                                      .operand = target}});
    }

    target = makeAstNode(ctx->pool,
                         &node->loc,
                         &(AstNode){.tag = astUnaryExpr,
                                    .type = type,
                                    .flags = node->flags,
                                    .unaryExpr = {.isPrefix = true,
                                                  .op = opDelete,
                                                  .operand = target}});
    return makeAstNode(ctx->pool,
                       &node->loc,
                       &(AstNode){.tag = astExprStmt,
                                  .flags = node->flags,
                                  .type = type,
                                  .exprStmt.expr = target});
}

bool visitSymbols(void *context, const void *it)
{
    AstNode *block = ((VisitSymbols *)context)->block;
    SemanticsContext *ctx = ((VisitSymbols *)context)->ctx;

    const Symbol *sym = it;
    const AstNode *node = sym->ref.node;
    const Type *type = node->type, *raw = stripPointer(type);

    if (!nodeIs(node, VarDecl) || hasFlag(node, ImmediatelyReturned) ||
        typeIs(raw, Opaque) || (isBuiltinType(type) && !typeIs(type, String)))
        return true;

    insertAstNode(&block->blockStmt.epilogue, makeDelete(ctx, node, type));

    return true;
}

void checkBlock(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *stmt = node->blockStmt.stmts;
    AstNode *prev = stmt;
    node->blockStmt.last = stmt;

    pushScope(ctx->env, node);

    for (; stmt; stmt = stmt->next) {
        stmt->parentScope = node;
        const Type *type = evalType(visitor, stmt);
        if (nodeIs(stmt, ReturnStmt)) {
            node->type = type;
        }
        if (stmt->tag == astDeferStmt) {
            // add statement to epilogue
            insertAstNode(&node->blockStmt.epilogue, stmt->deferStmt.expr);
            stmt->deferStmt.expr->flags |= flgDeferred;
            unlinkAstNode(&node->blockStmt.stmts, prev, stmt);
        }
        prev = stmt;
    }

    if (node->type == NULL) {
        node->blockStmt.last = prev;
        node->type = makeVoidType(ctx->typeTable);
    }

    //    VisitSymbols this = {.ctx = ctx, .block = node};
    //
    //    enumerateHashTable(
    //        &ctx->env->scope->symbols, &this, visitSymbols, sizeof(Symbol));

    popScope(ctx->env);
}
