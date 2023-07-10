/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-18
 */

#include "lang/codegen.h"
#include "lang/semantics.h"

#include "lang/capture.h"
#include "lang/flag.h"
#include "lang/ttable.h"
#include "lang/visitor.h"

#include "core/alloc.h"

#include <memory.h>

static void generateClosureForward(ConstAstVisitor *visitor,
                                   const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *params = node->funcDecl.params;

    generateTypeUsage(ctx, node->type->func.retType);
    format(ctx->state,
           " {s}_fwd(void *self",
           (FormatArg[]){{.s = node->funcDecl.name}});
    if (params->next)
        format(ctx->state, ", ", NULL);
    generateManyAstsWithDelim(visitor, "", ", ", ") {{{>}\n", params->next);

    if (node->type->func.retType->tag != typVoid) {
        format(ctx->state, "return ", NULL);
    }
    format(ctx->state, "{s}((", (FormatArg[]){{.s = node->funcDecl.name}});
    generateTypeUsage(ctx, node->funcDecl.params->type);
    format(ctx->state, ")self", NULL);

    for (const AstNode *param = params->next; param; param = param->next) {
        format(
            ctx->state, ", {s}", (FormatArg[]){{.s = param->funcParam.name}});
    }
    format(ctx->state, ");{<}\n}", (FormatArg[]){{.s = node->funcDecl.name}});
}

void generateClosureExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    TypeTable *table = (ctx)->types;

    const AstNode *parent = node->parentScope;
    bool isMember = nodeIs(parent, StructDecl);

    format(ctx->state, "attr(always_inline)\n", NULL);

    generateTypeUsage(ctx, node->type->func.retType);
    if (isMember) {
        format(ctx->state, " ", NULL);
        writeTypename(ctx, parent->type);
        format(ctx->state, "__{s}", (FormatArg[]){{.s = node->funcDecl.name}});
    }
    else {
        format(ctx->state, " {s}", (FormatArg[]){{.s = node->funcDecl.name}});
    }

    if (isMember) {
        format(ctx->state, "(", NULL);
        if (node->type->flags & flgConst)
            format(ctx->state, "const ", NULL);
        writeTypename(ctx, parent->type);
        format(ctx->state, " *this", NULL);
        if (node->funcDecl.params)
            format(ctx->state, ", ", NULL);

        generateManyAstsWithDelim(
            visitor, "", ", ", ")", node->funcDecl.params);
    }
    else {
        generateManyAstsWithDelim(
            visitor, "(", ", ", ")", node->funcDecl.params);
    }

    format(ctx->state, " ", NULL);
    if (node->funcDecl.body->tag == astBlockStmt) {
        astConstVisit(visitor, node->funcDecl.body);
    }
    else {
        format(ctx->state, "{{{>}\n", NULL);
        if (node->type->func.retType != makeVoidType(table)) {
            format(ctx->state, "return ", NULL);
        }
        astConstVisit(visitor, node->funcDecl.body);
        format(ctx->state, ";", NULL);
        format(ctx->state, "{<}\n}", NULL);
    }

    format(ctx->state, "\n", NULL);
    generateClosureForward(visitor, node);
}

void checkClosure(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *ret, **params;
    const AstNode *lastReturn = ctx->lastReturn;
    ctx->lastReturn = NULL;

    u64 paramsCount = countAstNodes(node->closureExpr.params) + 1;
    AstNode *param = node->closureExpr.params;
    u64 i = 1;

    if (node->closureExpr.ret) {
        node->type = evalType(visitor, node->closureExpr.ret);
        if (typeIs(node->type, Error))
            return;
    }

    pushScope(ctx->env, node);

    Scope *stack = ctx->closure;
    ctx->closure = ctx->env->scope;

    params = mallocOrDie(sizeof(Type *) * paramsCount);
    for (; param; param = param->next, i++) {
        param->parentScope = node;
        params[i] = evalType(visitor, param);
        if (hasFlag(param, Variadic)) {
            logError(ctx->L,
                     &param->loc,
                     "variadic parameters are not supported on closures",
                     NULL);
            node->type = ERROR_TYPE(ctx);
        }
    }

    if (typeIs(node->type, Error)) {
        ctx->closure = stack;
        popScope(ctx->env);
        return;
    }

    node->closureExpr.body->parentScope = node;
    ret = evalType(visitor, node->closureExpr.body);

    ctx->closure = stack;
    popScope(ctx->env);

    if (typeIs(ret, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    // We need to create a tuple for the capture
    u64 index = node->closureExpr.capture.index;
    const Type **capturedTypes = mallocOrDie(sizeof(Type *) * index);
    const char **names = allocFromMemPool(ctx->pool, sizeof(void *) * index);
    index = getOrderedCapture(
        &node->closureExpr.capture, capturedTypes, names, index);
    params[0] = makePointerType(
        ctx->typeTable,
        makeTupleType(ctx->typeTable, capturedTypes, index, flgNone),
        flgNone);
    free((void *)capturedTypes);

    AstNode *name = makeGenIdent(ctx->pool, ctx->strPool, &node->loc);
    node->type = makeFuncType(ctx->typeTable,
                              &(Type){.tag = typFunc,
                                      .name = NULL,
                                      .flags = node->flags | flgClosure,
                                      .func = {.retType = ret,
                                               .params = params,
                                               .captureNames = names,
                                               .capturedNamesCount = index,
                                               .paramsCount = paramsCount,
                                               .decl = name}});

    ctx->lastReturn = lastReturn;

    // We need to copy the closure node to global scope and replace it with
    // an identifier.
    AstNode *copy = copyAstNode(ctx->pool, node);
    __typeof(node->closureExpr) closureExpr = node->closureExpr;
    memset(&copy->closureExpr, 0, sizeof(closureExpr));
    copy->tag = astFuncDecl;
    copy->funcDecl.ret = closureExpr.ret;
    copy->funcDecl.params = makeAstNode(ctx->pool,
                                        &copy->loc,
                                        &(AstNode){.tag = astFuncParam,
                                                   .type = params[0],
                                                   .flags = flgCapture,
                                                   .next = closureExpr.params,
                                                   .funcParam = {
                                                       .name = "self",
                                                   }});

    copy->funcDecl.body = closureExpr.body;
    copy->funcDecl.name = name->ident.value;
    copy->flags |= flgClosure;

    addTopLevelDecl(ctx, copy->funcDecl.name, copy);

    node->next = NULL;
    node->tag = astPath;
    node->path.elements =
        makeAstNode(ctx->pool,
                    &node->loc,
                    &(AstNode){.tag = astPathElem,
                               .type = node->type,
                               .pathElement = {.name = copy->funcDecl.name}});

    free((void *)params);
}
