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

#include "lang/ttable.h"

#include "core/alloc.h"

#include <memory.h>

static inline const Type *makeFunctionDeclType(SemanticsContext *ctx,
                                               AstNode *node,
                                               const Type *ret,
                                               const Type **params,
                                               u64 paramsCount,
                                               bool hasDefaultValues)
{
    return makeFuncType(
        ctx->typeTable,
        &(Type){.tag = typFunc,
                .name = node->funcDecl.name,
                .flags = node->flags,
                .func = {.retType = ret,
                         .params = params,
                         .paramsCount = paramsCount,
                         .decl = node,
                         .defaultValuesCount = hasDefaultValues}});
}

static const Type *transformFuncTypeParam(SemanticsContext *ctx,
                                          const Type *type)
{
    // change (i32) => X, (&void, i32) => X
    const Type **newParams =
        mallocOrDie(sizeof(Type *) * type->func.paramsCount + 1);
    newParams[0] = makeVoidPointerType(ctx->typeTable, flgNone);
    for (u64 i = 0; i < type->func.paramsCount; i++)
        newParams[i + 1] = type->func.params[i];

    type =
        makeFuncType(ctx->typeTable,
                     &(Type){.tag = typFunc,
                             .flags = type->flags | flgFuncTypeParam,
                             .func = {.params = newParams,
                                      .paramsCount = type->func.paramsCount + 1,
                                      .retType = type->func.retType}});
    type = makeTupleType(
        ctx->typeTable,
        (const Type *[]){makeVoidPointerType(ctx->typeTable, flgNone), type},
        2,
        type->flags | flgFuncTypeParam);

    free(newParams);

    return type;
}

static const Type **checkFunctionParams(AstVisitor *visitor,
                                        AstNode *node,
                                        u64 *paramsCount,
                                        bool *withDefaultValues)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type **params = NULL;

    AstNode *param = node->funcDecl.params;
    bool isVariadic = false;

    *paramsCount = countAstNodes(node->funcDecl.params);
    if (*paramsCount == 0) {
        return NULL;
    }

    params = mallocOrDie(sizeof(Type *) * *paramsCount);

    for (u64 i = 0; param; param = param->next, i++) {
        param->parentScope = node;
        params[i] = evalType(visitor, param);
        if (isVariadic && (param->flags & flgVariadic)) {
            logError(ctx->L,
                     &param->loc,
                     "variadic parameters should the last parameter type in "
                     "function declaration",
                     NULL);
            continue;
        }

        isVariadic = (param->flags & flgVariadic);

        if (*withDefaultValues && param->funcParam.def == NULL) {
            logError(ctx->L,
                     &param->loc,
                     "parameter cannot be declared without a default value",
                     NULL);
            continue;
        }
        *withDefaultValues = (param->funcParam.def != NULL);
        if (params[i]->tag == typFunc) {
            params[i] = transformFuncTypeParam(ctx, params[i]);
            param->type = params[i];

            param->flags |= flgFuncTypeParam;
            node->flags |= flgClosureStyle;
        }
    }

    return params;
}

const Type *checkMethodDeclSignature(AstVisitor *visitor, AstNode *node)
{
    const Type *ret = NULL, **params, *type = NULL;
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    u64 paramsCount = 0;
    bool withDefaultValues = false;

    defineSymbol(&ctx->env, ctx->L, node->funcDecl.name, node);

    pushScope(&ctx->env, node);
    params =
        checkFunctionParams(visitor, node, &paramsCount, &withDefaultValues);

    ret = makeAutoType(ctx->typeTable);
    if (node->funcDecl.ret)
        ret = evalType(visitor, node->funcDecl.ret);

    node->type = makeFunctionDeclType(
        ctx, node, ret, params, paramsCount, withDefaultValues);

    free((void *)params);
    popScope(&ctx->env);

    return node->type;
}

void checkMethodDeclBody(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *ret = NULL, *type = NULL;
    AstNode *param = node->funcDecl.params;
    csAssert0(node->parentScope && nodeIs(node->parentScope, StructDecl));

    const Type *parent = makeThisType(ctx->typeTable,
                                      node->parentScope->structDecl.name,
                                      node->flags & flgConst);

    const AstNode *lastReturn = ctx->lastReturn;
    ctx->lastReturn = NULL;

    pushScope(&ctx->env, node);
    defineSymbol(&ctx->env,
                 ctx->L,
                 "this",
                 makeAstNode(ctx->pool,
                             &node->loc,
                             &(AstNode){.tag = astIdentifier,
                                        .flags = parent->flags,
                                        .type = parent,
                                        .ident.value = "this"}));

    if (node->parentScope->structDecl.base) {
        defineSymbol(
            &ctx->env,
            ctx->L,
            "super",
            makeAstNode(
                ctx->pool,
                &node->loc,
                &(AstNode){.tag = astIdentifier,
                           .flags = parent->flags | flgAddThis,
                           .type = node->parentScope->structDecl.base->type,
                           .ident.value = "super"}));
    }

    for (; param; param = param->next) {
        defineSymbol(&ctx->env, ctx->L, param->funcParam.name, param);
    }

    node->funcDecl.body->parentScope = node;
    ret = evalType(visitor, node->funcDecl.body);

    if (ctx->lastReturn && ret == makeVoidType(ctx->typeTable))
        ret = ctx->lastReturn->type;
    ctx->lastReturn = lastReturn;

    if (ret != node->type->func.retType)
        ((Type *)(node->type))->func.retType = ret;

    popScope(&ctx->env);
}

void checkFunctionDecl(AstVisitor *visitor, AstNode *node)
{
    const Type *ret = NULL, **params, *type = NULL;
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const AstNode *lastReturn = ctx->lastReturn;
    ctx->lastReturn = NULL;

    u64 paramsCount = 0;
    bool withDefaultValues = false;

    defineSymbol(&ctx->env, ctx->L, node->funcDecl.name, node);
    if (!ctx->mainOptimized) {
        node->flags |=
            (strcmp(node->funcDecl.name, "main") == 0) ? flgMain : flgNone;
        ctx->mainOptimized = node->flags & flgMain;
    }

    pushScope(&ctx->env, node);

    params =
        checkFunctionParams(visitor, node, &paramsCount, &withDefaultValues);

    ret = makeAutoType(ctx->typeTable);
    if (node->funcDecl.ret)
        ret = evalType(visitor, node->funcDecl.ret);

    type = makeFunctionDeclType(
        ctx, node, ret, params, paramsCount, withDefaultValues);
    node->type = type;

    if (node->funcDecl.body) {
        node->funcDecl.body->parentScope = node;
        ret = evalType(visitor, node->funcDecl.body);

        if (ctx->lastReturn && typeIs(ret, Void)) {
            ret = ctx->lastReturn->type;
        }
    }

    if (type->func.retType != ret) {
        node->type = makeFunctionDeclType(
            ctx, node, ret, params, paramsCount, withDefaultValues);
        removeFromTypeTable(ctx->typeTable, type);
    }

    ctx->lastReturn = lastReturn;

    free((void *)params);
    popScope(&ctx->env);
}
