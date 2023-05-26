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

AstNode *makeTypeReferenceNode(SemanticsContext *ctx, const Type *type)
{
    switch (type->tag) {
    case typPrimitive:
        return makeAstNode(ctx->pool,
                           builtinLoc(),
                           &(AstNode){.tag = astPrimitiveType,
                                      .type = type,
                                      .primitiveType.id = type->primitive.id});
    case typString:
        return makeAstNode(ctx->pool,
                           builtinLoc(),
                           &(AstNode){.tag = astStringType, .type = type});

    case typVoid:
        return makeAstNode(ctx->pool,
                           builtinLoc(),
                           &(AstNode){.tag = astVoidType, .type = type});

    case typArray:
        return makeAstNode(ctx->pool,
                           builtinLoc(),
                           &(AstNode){.tag = astArrayType, .type = type});
    case typStruct:
        return makeAstNode(
            ctx->pool,
            builtinLoc(),
            &(AstNode){
                .tag = astPath,
                .path = {
                    .elements = makeAstNode(
                        ctx->pool,
                        builtinLoc(),
                        &(AstNode){
                            .tag = astPathElem,
                            .pathElement = {
                                .name = type->tStruct.decl->structDecl.name,
                                .resolvesTo = type->tStruct.decl}})}});
    case typEnum:

    default:
        unreachable("TODO");
    }
}

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

    defineSymbol(ctx->env, ctx->L, name, node);

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
    defineSymbol(ctx->env,
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
    defineSymbol(ctx->env,
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
        return false;
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
                                getPrimitiveType(ctx->typeTable, prtU64),
                                makeDestructorType(ctx->typeTable)};
        addBuiltinFunc(ctx,
                       "__builtin_alloc",
                       makeVoidPointerType(ctx->typeTable, flgNone),
                       params,
                       3);
    }
    {
        const Type *params[] = {makeAutoType(ctx->typeTable)};
        addBuiltinFunc(
            ctx, "__builtin_dealloc", makeVoidType(ctx->typeTable), params, 1);
    }
    {
        const Type *params[] = {makeAutoType(ctx->typeTable),
                                makeVoidPointerType(ctx->typeTable, flgNone),
                                getPrimitiveType(ctx->typeTable, prtU64),
                                makeDestructorType(ctx->typeTable)};
        addBuiltinFunc(ctx,
                       "__builtin_realloc",
                       makeVoidPointerType(ctx->typeTable, flgNone),
                       params,
                       4);
    }
    {
        const Type *params[] = {
            makeTypeInfo(ctx->typeTable, getAnySliceType(ctx->typeTable)),
            getPrimitiveType(ctx->typeTable, prtU64),
            makeDestructorType(ctx->typeTable)};

        addBuiltinFunc(ctx,
                       "__builtin_alloc_slice",
                       getAnySliceType(ctx->typeTable),
                       params,
                       3);
    }

    {
        const Type *params[] = {
            makeTypeInfo(ctx->typeTable, getAnySliceType(ctx->typeTable)),
            getAnySliceType(ctx->typeTable),
            getPrimitiveType(ctx->typeTable, prtU64),
            makeDestructorType(ctx->typeTable)};

        addBuiltinFunc(ctx,
                       "__builtin_realloc_slice",
                       getAnySliceType(ctx->typeTable),
                       params,
                       4);
    }

    {
        const Type *params[] = {
            makeTypeInfo(ctx->typeTable, getAnySliceType(ctx->typeTable)),
            getAnySliceType(ctx->typeTable),
            getPrimitiveType(ctx->typeTable, prtI32)};

        addBuiltinFunc(ctx,
                       "__builtin_memset_slice",
                       makeVoidType(ctx->typeTable),
                       params,
                       3);
    }

    {
        const Type *params[] = {getAnySliceType(ctx->typeTable)};

        addBuiltinFunc(ctx,
                       "__builtin_init_slice",
                       makeVoidType(ctx->typeTable),
                       params,
                       1);
    }

    {
        const Type *params[] = {getAnySliceType(ctx->typeTable)};

        addBuiltinFunc(ctx,
                       "__builtin_free_slice",
                       makeVoidType(ctx->typeTable),
                       params,
                       1);
    }

    {
        // __builtin_assert
        const Type *params[] = {getPrimitiveType(ctx->typeTable, prtBool),
                                makeStringType(ctx->typeTable),
                                getPrimitiveType(ctx->typeTable, prtU64),
                                getPrimitiveType(ctx->typeTable, prtU64)};

        addBuiltinFunc(
            ctx, "__builtin_assert", makeVoidType(ctx->typeTable), params, 4);
    }
    {
        const Type *params[] = {makeAutoType(ctx->typeTable)};

        addBuiltinFunc(ctx,
                       "__builtin_sizeof",
                       getPrimitiveType(ctx->typeTable, prtU64),
                       params,
                       1);
    }

    {
        const Type *params[] = {makeStringType(ctx->typeTable)};

        addBuiltinFunc(
            ctx, "strlen", getPrimitiveType(ctx->typeTable, prtU64), params, 1);
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
                "target type '{t}' does not support native `#len` property",
                (FormatArg[]){{.t = target->type}});
        }
        return getPrimitiveType(ctx->typeTable, prtU64);
    }
    else {
        logError(ctx->L,
                 &target->loc,
                 "unknown type native property `{s}`",
                 (FormatArg[]){{.s = name}});
        return ERROR_TYPE(ctx);
    }
}
