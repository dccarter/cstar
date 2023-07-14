/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-21
 */

#include "lang/eval.h"
#include "lang/semantics.h"

#include "core/sb.h"
#include "lang/ast.h"
#include "lang/flag.h"
#include "lang/node.h"
#include "lang/strings.h"
#include "lang/ttable.h"
#include "lang/types.h"
#include "lang/visitor.h"

#include <string.h>

typedef struct {
    cstring name;
    EvaluateMacro eval;
} BuiltinMacro;

static inline bool validateMacroArgumentCount(SemanticsContext *ctx,
                                              const FileLoc *loc,
                                              const AstNode *args,
                                              u64 expected)
{
    u64 count = args ? countAstNodes(args) : 0;
    if (count != expected) {
        logError(ctx->L,
                 loc,
                 "unsupported number of arguments given to macro len, "
                 "expecting '{u64}', got '{u64}'",
                 (FormatArg[]){{.u64 = expected}, {.u64 = count}});
        return false;
    }
    return true;
}

static AstNode *makeFilenameNode(AstVisitor *visitor,
                                 attr(unused) const AstNode *node,
                                 attr(unused) AstNode *args)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 0))
        return NULL;

    return makeAstNode(
        ctx->pool,
        builtinLoc(),
        &(AstNode){.tag = astStringLit,
                   .type = makeStringType(ctx->typeTable),
                   .stringLiteral.value =
                       visitor->current->loc.fileName
                           ?: makeString(ctx->strPool, "<native>")});
}

static AstNode *makeLineNumberNode(AstVisitor *visitor,
                                   attr(unused) const AstNode *node,
                                   attr(unused) AstNode *args)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 0))
        return NULL;

    return makeAstNode(
        ctx->pool,
        builtinLoc(),
        &(AstNode){.tag = astIntegerLit,
                   .type = getPrimitiveType(ctx->typeTable, prtU64),
                   .intLiteral.value = visitor->current->loc.begin.row});
}

static AstNode *makeColumnNumberNode(AstVisitor *visitor,
                                     attr(unused) const AstNode *node,
                                     attr(unused) AstNode *args)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 0))
        return NULL;

    return makeAstNode(
        ctx->pool,
        builtinLoc(),
        &(AstNode){.tag = astIntegerLit,
                   .type = getPrimitiveType(ctx->typeTable, prtU64),
                   .intLiteral.value = visitor->current->loc.begin.col});
}

static AstNode *makeSizeofNode(AstVisitor *visitor,
                               attr(unused) const AstNode *node,
                               attr(unused) AstNode *args)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 1))
        return NULL;

    if (args->type == NULL)
        evalType(ctx->eval.semanticsVisitor, args);

    AstNode *sizeOf = findSymbolOnly(ctx->env, S___builtin_sizeof);
    csAssert0(sizeOf);

    return makeAstNode(
        ctx->pool,
        &node->loc,
        &(AstNode){
            .tag = astCallExpr,
            .flags = node->flags,
            .type = sizeOf->type->func.retType,
            .callExpr = {.callee = makeAstNode(
                             ctx->pool,
                             &node->loc,
                             &(AstNode){.tag = astIdentifier,
                                        .type = sizeOf->type,
                                        .flags = node->callExpr.callee->flags,
                                        .ident.value = S___builtin_sizeof}),
                         .args = args}});
}

static AstNode *makeAstIdentifierNode(AstVisitor *visitor,
                                      attr(unused) const AstNode *node,
                                      attr(unused) AstNode *args)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    if (args == NULL) {
        logError(ctx->L,
                 &node->loc,
                 "invalid number of arguments passed to `mkIdent!` macro, "
                 "expecting at least 1, got 0",
                 NULL);
        return NULL;
    }

    StringBuilder sb = {0};
    stringBuilderInit(&sb);

    for (AstNode *arg = args; arg;) {
        AstNode *it = arg;
        arg = arg->next;
        if (!evaluate(visitor, it)) {
            return NULL;
        }

        if (!evalStringBuilderAppend(ctx, &sb, it))
            return NULL;
    }

    memset(args, 0, sizeof(*args));

    char *str = stringBuilderRelease(&sb);
    args->tag = astIdentifier;
    args->ident.value = makeString(ctx->strPool, str);
    free(str);
    args->flags |= flgVisited;
    return args;
}

static AstNode *makeAstIntegerNode(AstVisitor *visitor,
                                   attr(unused) const AstNode *node,
                                   attr(unused) AstNode *args)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 1))
        return NULL;

    if (!evaluate(visitor, args) || !isIntegralLiteral(args)) {
        logError(ctx->L,
                 &node->loc,
                 "invalid argument type passed to `mkInteger!` macro, "
                 "expecting an integral type",
                 NULL);
        return NULL;
    }

    return args;
}

static AstNode *makeTypeinfoNode(AstVisitor *visitor, const Type *type)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    AstNode *info = makeTypeReferenceNode(ctx, type);
    info->flags |= flgTypeinfo;
    info->type = makeTypeInfo(ctx->typeTable, info->type);
    return info;
}

static AstNode *makeLenNode(AstVisitor *visitor,
                            const AstNode *node,
                            AstNode *args)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 1))
        return NULL;

    const Type *type = args->type ?: evalType(ctx->eval.semanticsVisitor, args);
    const Type *raw = stripAll(type);

    switch (raw->tag) {
    case typString: {
        if (nodeIs(args, StringLit)) {
            args->tag = astIntegerLit;
            args->type = getPrimitiveType(ctx->typeTable, prtU64);
            u64 len = strlen(args->stringLiteral.value);
            memset(&args->stringLiteral, 0, sizeof(args->stringLiteral));
            args->intLiteral.value = (i64)len;
            return args;
        }
        else {
            AstNode *strLen = findSymbolOnly(ctx->env, S_strlen);
            csAssert0(strLen);
            return makeAstNode(
                ctx->pool,
                &node->loc,
                &(AstNode){
                    .tag = astCallExpr,
                    .type = strLen->type->func.retType,
                    .callExpr = {
                        .callee = makeAstNode(
                            ctx->pool,
                            &node->loc,
                            &(AstNode){.tag = astIdentifier,
                                       .type = strLen->type,
                                       .flags = strLen->flags | node->flags,
                                       .ident.value = S_strlen}),
                        .args = args}});
        }
    }
    case typArray:
        if (raw->array.len == UINT64_MAX) {
            return makeAstNode(
                ctx->pool,
                &node->loc,
                &(AstNode){
                    .tag = astMemberExpr,
                    .flags = flgVisited,
                    .type = getPrimitiveType(ctx->typeTable, prtU64),
                    .memberExpr = {.target = args,
                                   .member = makeAstNode(
                                       ctx->pool,
                                       &node->loc,
                                       &(AstNode){.tag = astIdentifier,
                                                  .flags = flgConst,
                                                  .type = getPrimitiveType(
                                                      ctx->typeTable, prtU64),
                                                  .ident.value = S_len})}});
        }
        return makeAstNode(
            ctx->pool,
            &node->loc,
            &(AstNode){
                .tag = astGroupExpr,
                .type = getPrimitiveType(ctx->typeTable, prtU64),
                .groupExpr.expr = makeAstNode(
                    ctx->pool,
                    &node->loc,
                    &(AstNode){
                        .tag = astBinaryExpr,
                        .type = getPrimitiveType(ctx->typeTable, prtU64),
                        .binaryExpr = {
                            .op = opDiv,
                            .lhs = makeSizeofNode(visitor, node, args),
                            .rhs = makeSizeofNode(
                                visitor,
                                node,
                                makeTypeinfoNode(visitor,
                                                 raw->array.elementType))}})});

    case typStruct: {
        AstNode *symbol =
            findSymbol(raw->tStruct.decl->env, ctx->L, S_len, &args->loc);
        if (nodeIs(symbol, StructField) && isUnsignedType(symbol->type)) {
            return makeAstNode(
                ctx->pool,
                &node->loc,
                &(AstNode){
                    .tag = astMemberExpr,
                    .flags = args->flags,
                    .type = symbol->type,
                    .memberExpr = {.target = args,
                                   .member = makeAstNode(
                                       ctx->pool,
                                       &args->loc,
                                       &(AstNode){.tag = astIdentifier,
                                                  .type = symbol->type,
                                                  .flags = symbol->flags,
                                                  .ident.value = S_len})}});
        }
        break;
    }
    default:
        break;
    }

    logError(ctx->L,
             &args->loc,
             "macro native 'len!' cannot be used with expression of type '{t}'",
             (FormatArg[]){{.t = type}});
    return NULL;
}

static AstNode *makeDataNode(AstVisitor *visitor,
                             const AstNode *node,
                             AstNode *args)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 1))
        return NULL;

    const Type *type = args->type ?: evalType(ctx->eval.semanticsVisitor, args);
    const Type *raw = stripAll(type);

    switch (raw->tag) {
    case typArray:
        if (raw->array.len == UINT64_MAX) {
            const Type *retType = makePointerType(
                ctx->typeTable, raw->array.elementType, type->flags & flgConst);
            return makeAstNode(
                ctx->pool,
                &node->loc,
                &(AstNode){
                    .tag = astMemberExpr,
                    .type = retType,
                    .memberExpr = {.target = args,
                                   .member = makeAstNode(
                                       ctx->pool,
                                       &node->loc,
                                       &(AstNode){.tag = astIdentifier,
                                                  .flags = flgConst,
                                                  .type = retType,
                                                  .ident.value = S_data})}});
        }

    default:
        break;
    }

    logError(ctx->L,
             &args->loc,
             "macro native 'data!' cannot be used with expression of type "
             "'{t}'",
             (FormatArg[]){{.t = type}});
    return NULL;
}

static AstNode *makeAssertNode(AstVisitor *visitor,
                               const AstNode *node,
                               AstNode *args)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 1))
        return NULL;

    const Type *type = args->type ?: evalType(ctx->eval.semanticsVisitor, args);
    if (!typeIs(type, Primitive) || type->primitive.id != prtBool) {
        logError(ctx->L,
                 &args->loc,
                 "invalid `assert!` macro argument type, expecting 'bool'"
                 ", got '{t}'",
                 (FormatArg[]){{.t = type}});
    }

    AstNode *builtinAssert =
        findSymbolOnly(getBuiltinEnv(), S___builtin_assert);
    csAssert0(builtinAssert);
    AstNode *next = args;
    next = next->next = makeFilenameNode(visitor, node, NULL);
    next = next->next = makeLineNumberNode(visitor, node, NULL);
    next->next = makeColumnNumberNode(visitor, node, NULL);

    return makeAstNode(
        ctx->pool,
        &node->loc,
        &(AstNode){
            .tag = astCallExpr,
            .flags = node->flags,
            .type = builtinAssert->type->func.retType,
            .callExpr = {
                .callee = makeAstNode(
                    ctx->pool,
                    &node->macroCallExpr.callee->loc,
                    &(AstNode){.tag = astIdentifier,
                               .flags = node->macroCallExpr.callee->flags,
                               .type = builtinAssert->type,
                               .ident.value = S___builtin_assert}),
                .args = args}});
}

static AstNode *makeUncheckedNode(AstVisitor *visitor,
                                  const AstNode *node,
                                  AstNode *args)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 2))
        return NULL;
    AstNode *expr = args, *next = args->next;
    const Type *type = next->type ?: evalType(ctx->eval.semanticsVisitor, next);
    if (!typeIs(type, Info)) {
        logError(ctx->L,
                 &next->loc,
                 "invalid `unchecked!` macro parameter, expecting a "
                 "`@typeinfo`, got '{t}'",
                 (FormatArg[]){{.t = type}});
        return NULL;
    }

    expr->type = type->info.target;
    expr->flags |= flgVisited;

    if (type->info.target)
        return makeAstNode(
            ctx->pool,
            &node->loc,
            &(AstNode){
                .tag = astCastExpr,
                .type = type->info.target,
                .flags = expr->flags,
                .castExpr = {
                    .expr = expr,
                    .to = makeAstNode(ctx->pool,
                                      &expr->loc,
                                      &(AstNode){.tag = astNop,
                                                 .type = type->info.target})}});

    return expr;
}

static AstNode *makeCstrNode(AstVisitor *visitor,
                             const AstNode *node,
                             AstNode *args)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 1))
        return NULL;

    const Type *type = args->type ?: evalType(ctx->eval.semanticsVisitor, args);
    if (!typeIs(type, String)) {
        logError(ctx->L,
                 &args->loc,
                 "unexpected expression type passed `cstr!` macro, expecting "
                 "'string', got {t}",
                 (FormatArg[]){{.t = type}});
        return NULL;
    }

    args->type = makePointerType(
        ctx->typeTable, getPrimitiveType(ctx->typeTable, prtI8), flgConst);
    args->flags |= flgVisited;
    return args;
}

static AstNode *makeDestructorNode(AstVisitor *visitor,
                                   const AstNode *node,
                                   AstNode *args)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 1))
        return NULL;

    const Type *type = args->type ?: evalType(ctx->eval.semanticsVisitor, args);
    if (!typeIs(type, Info)) {
        logError(ctx->L,
                 &args->loc,
                 "invalid `destructor!` macro parameter, expecting a "
                 "`@typeinfo`, got '{t}'",
                 (FormatArg[]){{.t = type}});
        return NULL;
    }

    args->type = makeDestructorType(ctx->typeTable, ctx->strPool);
    args->tag = astDestructorRef;
    args->flags = flgVisited;
    memset(&args->_body, 0, CXY_AST_NODE_BODY_SIZE);
    args->destructorRef.target = type->info.target;

    return args;
}

static AstNode *makeTypeofNode(AstVisitor *visitor,
                               const AstNode *node,
                               AstNode *args)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 1))
        return NULL;

    const Type *type = args->type ?: evalType(ctx->eval.semanticsVisitor, args);
    if (typeIs(type, Info)) {
        logError(ctx->L,
                 &node->loc,
                 "invalid `typeof!` macro argument, argument is already an "
                 "`@typeinfo` object",
                 (FormatArg[]){{.t = type}});
        return NULL;
    }
    u64 flags = flgNone;
    type = unwrapType(type, &flags);

    return makeTypeinfoNode(ctx->eval.semanticsVisitor, type);
}

static AstNode *makeBaseOfNode(AstVisitor *visitor,
                               const AstNode *node,
                               AstNode *args)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 1))
        return NULL;

    const Type *type = args->type ?: evalType(ctx->eval.semanticsVisitor, args);
    if (!typeIs(type, Info)) {
        logError(ctx->L,
                 &node->loc,
                 "invalid `typeof!` macro argument, expecting a type info "
                 "object",
                 NULL);
        return NULL;
    }

    type = type->info.target;
    if (!typeIs(type, Enum) && !typeIs(type, Struct)) {
        logError(ctx->L,
                 &node->loc,
                 "invalid `typeof!` macro argument, unexpected type '{t}', "
                 "expecting a struct or enum type",
                 (FormatArg[]){{.t = type}});
        return NULL;
    }

    type = typeIs(type, Enum)
               ? type->tEnum.base
               : (type->tStruct.base ?: makeVoidType(ctx->typeTable));

    args->type = makeTypeInfo(ctx->typeTable, type);
    return args;
}

static AstNode *makePointerOfNode(AstVisitor *visitor,
                                  const AstNode *node,
                                  AstNode *args)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 1))
        return NULL;

    Operator op = args->binaryExpr.op;
    if (!nodeIs(args, BinaryExpr) || (op != opAdd && op != opMinus)) {
        logError(ctx->L,
                 &args->loc,
                 "unexpected expression passed to `ptroff`, expecting binary "
                 "`+` or `-` expression",
                 NULL);
        return NULL;
    }
    const Type *lhs =
        args->binaryExpr.lhs->type
            ?: evalType(ctx->eval.semanticsVisitor, args->binaryExpr.lhs);
    if (!typeIs(lhs, Pointer)) {
        logError(ctx->L,
                 &args->binaryExpr.lhs->loc,
                 "unexpected expression passed to `ptroff`, expecting a "
                 "pointer expression",
                 NULL);
        return NULL;
    }

    const Type *rhs =
        args->binaryExpr.rhs->type
            ?: evalType(ctx->eval.semanticsVisitor, args->binaryExpr.rhs);
    if (!isIntegerType(rhs)) {
        logError(ctx->L,
                 &args->binaryExpr.rhs->loc,
                 "unexpected expression passed to `ptroff`, expecting an "
                 "expression",
                 NULL);
        return NULL;
    }

    args->type = lhs;
    args->flags |= flgVisited;
    return args;
}

static bool compareBuiltinMacros(const void *lhs, const void *rhs)
{
    return ((BuiltinMacro *)lhs)->name == ((BuiltinMacro *)rhs)->name;
}

static HashTable *builtinMacros = NULL;

static HashTable *getBuiltinMacros()
{
    if (builtinMacros == NULL) {
        static HashTable _builtinMacros;
        _builtinMacros = newHashTable(sizeof(BuiltinMacro));
        insertInHashTable(&_builtinMacros,
                          &(BuiltinMacro){.name = S__assert, makeAssertNode},
                          hashUint64(hashInit(), (uintptr_t)S__assert),
                          sizeof(BuiltinMacro),
                          compareBuiltinMacros);
        insertInHashTable(&_builtinMacros,
                          &(BuiltinMacro){.name = S_base_of, makeBaseOfNode},
                          hashUint64(hashInit(), (uintptr_t)S_base_of),
                          sizeof(BuiltinMacro),
                          compareBuiltinMacros);
        insertInHashTable(
            &_builtinMacros,
            &(BuiltinMacro){.name = S_column, makeColumnNumberNode},
            hashUint64(hashInit(), (uintptr_t)S_column),
            sizeof(BuiltinMacro),
            compareBuiltinMacros);
        insertInHashTable(&_builtinMacros,
                          &(BuiltinMacro){.name = S_cstr, makeCstrNode},
                          hashUint64(hashInit(), (uintptr_t)S_cstr),
                          sizeof(BuiltinMacro),
                          compareBuiltinMacros);
        insertInHashTable(&_builtinMacros,
                          &(BuiltinMacro){.name = S_data, makeDataNode},
                          hashUint64(hashInit(), (uintptr_t)S_data),
                          sizeof(BuiltinMacro),
                          compareBuiltinMacros);
        insertInHashTable(
            &_builtinMacros,
            &(BuiltinMacro){.name = S_destructor, makeDestructorNode},
            hashUint64(hashInit(), (uintptr_t)S_destructor),
            sizeof(BuiltinMacro),
            compareBuiltinMacros);
        insertInHashTable(&_builtinMacros,
                          &(BuiltinMacro){.name = S_file, makeFilenameNode},
                          hashUint64(hashInit(), (uintptr_t)S_file),
                          sizeof(BuiltinMacro),
                          compareBuiltinMacros);
        insertInHashTable(&_builtinMacros,
                          &(BuiltinMacro){.name = S_len, makeLenNode},
                          hashUint64(hashInit(), (uintptr_t)S_len),
                          sizeof(BuiltinMacro),
                          compareBuiltinMacros);
        insertInHashTable(&_builtinMacros,
                          &(BuiltinMacro){.name = S_line, makeLineNumberNode},
                          hashUint64(hashInit(), (uintptr_t)S_line),
                          sizeof(BuiltinMacro),
                          compareBuiltinMacros);
        insertInHashTable(
            &_builtinMacros,
            &(BuiltinMacro){.name = S_mkIdent, makeAstIdentifierNode},
            hashUint64(hashInit(), (uintptr_t)S_mkIdent),
            sizeof(BuiltinMacro),
            compareBuiltinMacros);
        insertInHashTable(
            &_builtinMacros,
            &(BuiltinMacro){.name = S_mkInteger, makeAstIntegerNode},
            hashUint64(hashInit(), (uintptr_t)S_mkInteger),
            sizeof(BuiltinMacro),
            compareBuiltinMacros);
        insertInHashTable(&_builtinMacros,
                          &(BuiltinMacro){.name = S_ptroff, makePointerOfNode},
                          hashUint64(hashInit(), (uintptr_t)S_ptroff),
                          sizeof(BuiltinMacro),
                          compareBuiltinMacros);
        insertInHashTable(&_builtinMacros,
                          &(BuiltinMacro){.name = S_sizeof, makeSizeofNode},
                          hashUint64(hashInit(), (uintptr_t)S_sizeof),
                          sizeof(BuiltinMacro),
                          compareBuiltinMacros);
        insertInHashTable(&_builtinMacros,
                          &(BuiltinMacro){.name = S_typeof, makeTypeofNode},
                          hashUint64(hashInit(), (uintptr_t)S_typeof),
                          sizeof(BuiltinMacro),
                          compareBuiltinMacros);
        insertInHashTable(
            &_builtinMacros,
            &(BuiltinMacro){.name = S_unchecked, makeUncheckedNode},
            hashUint64(hashInit(), (uintptr_t)S_unchecked),
            sizeof(BuiltinMacro),
            compareBuiltinMacros);
        builtinMacros = &_builtinMacros;
    }
    return builtinMacros;
}

static EvaluateMacro findBuiltinMacro(cstring name)
{
    BuiltinMacro find = {.name = name};
    BuiltinMacro *found =
        findInHashTable(builtinMacros ?: getBuiltinMacros(),
                        &find,
                        hashUint64(hashInit(), (uint64_t)name),
                        sizeof(find),
                        compareBuiltinMacros);

    return found ? found->eval : NULL;
}

static EvaluateMacro findBuiltinMacroByNode(AstNode *node)
{
    if (nodeIs(node, Identifier))
        return findBuiltinMacro(node->ident.value);
    if (nodeIs(node, Path) && node->path.elements->next == NULL)
        return findBuiltinMacro(node->path.elements->pathElement.name);
    return NULL;
}

void evalMacroCall(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    EvaluateMacro macro = findBuiltinMacroByNode(node->macroCallExpr.callee);
    if (macro == NULL) {
        logError(ctx->L,
                 &node->macroCallExpr.callee->loc,
                 "currently only native macros are supported",
                 NULL);
        node->type = ERROR_TYPE(ctx);
        return;
    }
    AstNode *substitute = macro(visitor, node, node->macroCallExpr.args);
    if (!substitute) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    substitute->next = node->next;
    substitute->parentScope = node->parentScope;
    *node = *substitute;
}
