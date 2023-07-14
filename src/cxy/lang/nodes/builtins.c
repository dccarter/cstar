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

#include "lang/flag.h"
#include "lang/strings.h"
#include "lang/ttable.h"
#include "lang/visitor.h"

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
    case typTuple:
        return makeAstNode(ctx->pool,
                           builtinLoc(),
                           &(AstNode){.tag = astTupleType, .type = type});
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
    case typPointer:
        return makeAstNode(
            ctx->pool,
            builtinLoc(),
            &(AstNode){.tag = astPointerType,
                       .pointerType = {.pointed = makeTypeReferenceNode(
                                           ctx, stripAll(type))}});
    case typWrapped:
        return makeTypeReferenceNode(ctx, unwrapType(type, NULL));

    case typOpaque:
        return makeAstNode(ctx->pool,
                           builtinLoc(),
                           &(AstNode){.tag = astTypeDecl, .type = type});

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
    AstNode *node = makeAstNode(ctx->pool,
                                builtinLoc(),
                                &(AstNode){.tag = astFuncDecl,
                                           .flags = flgBuiltin | flgPublic,
                                           .type = NULL});

    defineDeclaration(ctx, name, NULL, node, true);

    node->type = makeFuncType(ctx->typeTable,
                              &(Type){.tag = typFunc,
                                      .name = name,
                                      .flags = flgBuiltin | flgPublic,
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
    defineDeclaration(
        ctx,
        name,
        NULL,
        makeAstNode(ctx->pool,
                    builtinLoc(),
                    &(AstNode){.tag = astVarDecl,
                               .flags = flgBuiltin | flgConst | flgPublic,
                               .type = type,
                               .varDecl = {.names = makeAstNode(
                                               ctx->pool,
                                               builtinLoc(),
                                               &(AstNode){.tag = astIdentifier,
                                                          .ident.value = name}),
                                           .init = value}}),
        true);
}

static void addBuiltinType(SemanticsContext *ctx,
                           cstring name,
                           u64 flags,
                           const Type *type)
{
    defineDeclaration(
        ctx,
        name,
        NULL,
        makeAstNode(ctx->pool,
                    builtinLoc(),
                    &(AstNode){.tag = astTypeDecl,
                               .flags = flgBuiltin | flags | flgPublic,
                               .type = type,
                               .typeDecl.name = name}),
        true);
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
            ctx, S_wputc, getPrimitiveType(ctx->typeTable, prtI32), params, 1);
    }
    {
        const Type *params[] = {
            makeAutoType(ctx->typeTable),
            getPrimitiveType(ctx->typeTable, prtU64),
            makeDestructorType(ctx->typeTable, ctx->strPool)};
        addBuiltinFunc(ctx,
                       S___builtin_alloc,
                       makeVoidPointerType(ctx->typeTable, flgNone),
                       params,
                       3);
    }
    {
        const Type *params[] = {makeAutoType(ctx->typeTable)};
        addBuiltinFunc(
            ctx, S___builtin_dealloc, makeVoidType(ctx->typeTable), params, 1);
    }
    {
        const Type *params[] = {
            makeAutoType(ctx->typeTable),
            makeVoidPointerType(ctx->typeTable, flgNone),
            getPrimitiveType(ctx->typeTable, prtU64),
            makeDestructorType(ctx->typeTable, ctx->strPool)};
        addBuiltinFunc(ctx,
                       S___builtin_realloc,
                       makeVoidPointerType(ctx->typeTable, flgNone),
                       params,
                       4);
    }
    {
        const Type *params[] = {
            makeTypeInfo(ctx->typeTable, getAnySliceType(ctx->typeTable)),
            getPrimitiveType(ctx->typeTable, prtU64),
            makeDestructorType(ctx->typeTable, ctx->strPool)};

        addBuiltinFunc(ctx,
                       S___builtin_alloc_slice,
                       getAnySliceType(ctx->typeTable),
                       params,
                       3);
    }

    {
        const Type *params[] = {
            makeTypeInfo(ctx->typeTable, getAnySliceType(ctx->typeTable)),
            getAnySliceType(ctx->typeTable),
            getPrimitiveType(ctx->typeTable, prtU64),
            makeDestructorType(ctx->typeTable, ctx->strPool)};

        addBuiltinFunc(ctx,
                       S___builtin_realloc_slice,
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
                       S___builtin_memset_slice,
                       makeVoidType(ctx->typeTable),
                       params,
                       3);
    }

    {
        const Type *params[] = {getAnySliceType(ctx->typeTable)};

        addBuiltinFunc(ctx,
                       S___builtin_init_slice,
                       makeVoidType(ctx->typeTable),
                       params,
                       1);
    }

    {
        const Type *params[] = {getAnySliceType(ctx->typeTable)};

        addBuiltinFunc(ctx,
                       S___builtin_free_slice,
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
            ctx, S___builtin_assert, makeVoidType(ctx->typeTable), params, 4);
    }
    {
        const Type *params[] = {makeAutoType(ctx->typeTable)};

        addBuiltinFunc(ctx,
                       S___builtin_sizeof,
                       getPrimitiveType(ctx->typeTable, prtU64),
                       params,
                       1);
    }

    {
        const Type *params[] = {makeStringType(ctx->typeTable)};

        addBuiltinFunc(
            ctx, S_strlen, getPrimitiveType(ctx->typeTable, prtU64), params, 1);
    }

    {
        addBuiltinType(
            ctx, S_char, flgNative, getPrimitiveType(ctx->typeTable, prtI8));

        addBuiltinType(ctx,
                       S_cxy_range_t,
                       flgNative,
                       makeOpaqueType(ctx->typeTable, S_cxy_range_t));
    }
}
