/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-19
 */

#include "lang/semantics.h"

#include "lang/ttable.h"

#include <string.h>

static void addBuiltinFunc(SemanticsContext *ctx,
                           cstring name,
                           const Type *ret,
                           const Type **params,
                           u64 paramsCount)
{
    AstNode *node = makeAstNode(
        ctx->pool,
        builtinLoc(),
        &(AstNode){.tag = astFuncDecl, .flags = flgBuiltin, .type = NULL});

    defineSymbol(&ctx->env, ctx->L, name, node);

    node->type = makeFuncType(ctx->typeTable,
                              &(Type){.tag = typFunc,
                                      .name = name,
                                      .flags = flgBuiltin,
                                      .func = {.retType = ret,
                                               .params = params,
                                               .paramsCount = paramsCount,
                                               .decl = node}});
}

static void addBuiltinVariable(SemanticsContext *ctx,
                               cstring name,
                               const Type *type,
                               AstNode *value)
{
    defineSymbol(&ctx->env,
                 ctx->L,
                 name,
                 makeAstNode(ctx->pool,
                             builtinLoc(),
                             &(AstNode){.tag = astVarDecl,
                                        .flags = flgBuiltin | flgConst,
                                        .type = type}));
}

static void addBuiltinType(SemanticsContext *ctx,
                           cstring name,
                           u64 flags,
                           const Type *type)
{
    defineSymbol(&ctx->env,
                 ctx->L,
                 name,
                 makeAstNode(ctx->pool,
                             builtinLoc(),
                             &(AstNode){.tag = astTypeDecl,
                                        .flags = flgBuiltin | flags,
                                        .type = type}));
}

static inline bool typeSupportsLenProperty(const Type *type)
{
    switch (stripPointer(type)->tag) {
    case typArray:
        return isSliceType(type);
    default:
        false;
    }
}

void initializeBuiltins(SemanticsContext *ctx)
{
    {
        const Type *params[] = {getPrimitiveType(ctx->typeTable, prtChar)};
        addBuiltinFunc(
            ctx, "wputc", getPrimitiveType(ctx->typeTable, prtI32), params, 1);
    }
    {
        const Type *params[] = {makeVoidPointerType(ctx->typeTable, flgConst)};
        addBuiltinFunc(
            ctx, "ptr", getPrimitiveType(ctx->typeTable, prtU64), params, 1);
    }
    {
        const Type *params[] = {makeAutoType(ctx->typeTable),
                                getPrimitiveType(ctx->typeTable, prtU64)};
        addBuiltinFunc(ctx,
                       "__builtin_alloc",
                       makeVoidPointerType(ctx->typeTable, flgNone),
                       params,
                       2);
    }
    {
        const Type *params[] = {
            makeTypeInfo(ctx->typeTable, getAnySliceType(ctx->typeTable)),
            getPrimitiveType(ctx->typeTable, prtU64)};

        addBuiltinFunc(ctx,
                       "__builtin_alloc_slice",
                       getAnySliceType(ctx->typeTable),
                       params,
                       2);
    }

    {
        addBuiltinType(
            ctx, "char", flgNative, makeOpaqueType(ctx->typeTable, "char"));

        addBuiltinType(ctx,
                       "cxy_range_t",
                       flgNative,
                       makeOpaqueType(ctx->typeTable, "cxy_range_t"));
    }
}

const Type *checkBuiltinTypeProperty(SemanticsContext *ctx,
                                     AstNode *target,
                                     cstring name)
{
    if (strcmp(name, "len") == 0) {
        if (typeSupportsLenProperty(target->type)) {
            logError(
                ctx->L,
                &target->loc,
                "target type '{t}' does not support builtin `#len` property",
                (FormatArg[]){{.t = target->type}});
        }
        return getPrimitiveType(ctx->typeTable, prtU64);
    }
    else {
        logError(ctx->L,
                 &target->loc,
                 "unknown type builtin property `{s}`",
                 (FormatArg[]){{.s = name}});
        return ERROR_TYPE(ctx);
    }
}
