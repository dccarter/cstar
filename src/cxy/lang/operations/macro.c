/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-21
 */

#include "macro.h"
#include "eval.h"

#include "lang/ast.h"
#include "lang/builtins.h"
#include "lang/capture.h"
#include "lang/flag.h"
#include "lang/strings.h"
#include "lang/ttable.h"
#include "lang/types.h"

#include "core/sb.h"

#include <string.h>

typedef struct {
    cstring name;
    EvaluateMacro eval;
} BuiltinMacro;

static inline bool validateMacroArgumentCount(EvalContext *ctx,
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
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 0))
        return NULL;

    return makeAstNode(
        ctx->pool,
        builtinLoc(),
        &(AstNode){.tag = astStringLit,
                   .type = makeStringType(ctx->types),
                   .stringLiteral.value =
                       visitor->current->loc.fileName ?: "<native>"});
}

static AstNode *makeLineNumberNode(AstVisitor *visitor,
                                   attr(unused) const AstNode *node,
                                   attr(unused) AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 0))
        return NULL;

    return makeAstNode(
        ctx->pool,
        builtinLoc(),
        &(AstNode){.tag = astIntegerLit,
                   .type = getPrimitiveType(ctx->types, prtU64),
                   .intLiteral.value = visitor->current->loc.begin.row});
}

static AstNode *makeColumnNumberNode(AstVisitor *visitor,
                                     attr(unused) const AstNode *node,
                                     attr(unused) AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 0))
        return NULL;

    return makeAstNode(
        ctx->pool,
        builtinLoc(),
        &(AstNode){.tag = astIntegerLit,
                   .type = getPrimitiveType(ctx->types, prtU64),
                   .intLiteral.value = visitor->current->loc.begin.col});
}

static AstNode *makeSizeofNode(AstVisitor *visitor,
                               attr(unused) const AstNode *node,
                               attr(unused) AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 1))
        return NULL;

    AstNode *sizeOf = findBuiltinDecl(S_CXY__builtins_sizeof);
    csAssert0(sizeOf);

    return makeCallExpr(ctx->pool,
                        &node->loc,
                        makeResolvedPath(ctx->pool,
                                         &node->loc,
                                         S_CXY__builtins_sizeof,
                                         node->callExpr.callee->flags,
                                         sizeOf,
                                         sizeOf->type),
                        args,
                        node->flags,
                        NULL,
                        sizeOf->type->func.retType);
}

static AstNode *makeAstIdentifierNode(AstVisitor *visitor,
                                      attr(unused) const AstNode *node,
                                      attr(unused) AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
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
    args->ident.value = makeString(ctx->strings, str);
    free(str);
    args->flags |= flgVisited;
    return args;
}

static AstNode *makeAstIntegerNode(AstVisitor *visitor,
                                   attr(unused) const AstNode *node,
                                   attr(unused) AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (args == NULL) {
        logError(ctx->L,
                 &node->loc,
                 "invalid number of arguments passed to `mkIdent!` macro, "
                 "expecting at least 1, got 0",
                 NULL);
        return NULL;
    }

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

static AstNode *makeIsTypeNode(AstVisitor *visitor,
                               attr(unused) const AstNode *node,
                               attr(unused) AstNode *args,
                               cstring name,
                               TTag tag)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 1))
        return NULL;

    if (!typeIs(args->type, Info)) {
        logError(ctx->L,
                 &args->loc,
                 "macro native '{s}!' expecting a typeinfo object",
                 (FormatArg[]){{.s = name}});
        return NULL;
    }

    clearAstBody(args);
    args->flags = flgNone;
    args->tag = astBoolLit;
    args->boolLiteral.value = args->type->info.target->tag == tag;
    args->type = getPrimitiveType(ctx->types, prtBool);

    return args;
}

static AstNode *makeIsPointerNode(AstVisitor *visitor,
                                  attr(unused) const AstNode *node,
                                  attr(unused) AstNode *args)
{
    return makeIsTypeNode(visitor, node, args, "is_pointer", typPointer);
}

static AstNode *makeIsEnumNode(AstVisitor *visitor,
                               attr(unused) const AstNode *node,
                               attr(unused) AstNode *args)
{
    return makeIsTypeNode(visitor, node, args, "is_enum", typEnum);
}

static AstNode *makeIsStructNode(AstVisitor *visitor,
                                 attr(unused) const AstNode *node,
                                 attr(unused) AstNode *args)
{
    return makeIsTypeNode(visitor, node, args, "is_struct", typStruct);
}

static AstNode *makeTypeinfoNode(AstVisitor *visitor,
                                 const FileLoc *loc,
                                 const Type *type)
{
    EvalContext *ctx = getAstVisitorContext(visitor);

    AstNode *info = makeTypeReferenceNode(ctx->pool, type, loc);
    info->flags |= flgTypeinfo;
    info->type = makeTypeInfo(ctx->types, info->type);
    return info;
}

static AstNode *makeLenNode(AstVisitor *visitor,
                            const AstNode *node,
                            AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 1))
        return NULL;

    const Type *type = args->type ?: evalType(ctx, args);
    csAssert0(type);
    const Type *raw = stripAll(type);

    switch (raw->tag) {
    case typString: {
        if (nodeIs(args, StringLit)) {
            args->tag = astIntegerLit;
            args->type = getPrimitiveType(ctx->types, prtU64);
            u64 len = strlen(args->stringLiteral.value);
            memset(&args->stringLiteral, 0, sizeof(args->stringLiteral));
            args->intLiteral.value = (i64)len;
            return args;
        }
        else {
            AstNode *strLen = findBuiltinDecl(S_strlen);
            csAssert0(strLen);
            return makeCallExpr(ctx->pool,
                                &node->loc,
                                makeResolvedPath(ctx->pool,
                                                 &node->loc,
                                                 S_strlen,
                                                 strLen->flags | node->flags,
                                                 strLen,
                                                 strLen->type),
                                args,
                                node->flags,
                                NULL,
                                strLen->type->func.retType);
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
                    .type = getPrimitiveType(ctx->types, prtU64),
                    .memberExpr = {.target = args,
                                   .member = makeAstNode(
                                       ctx->pool,
                                       &node->loc,
                                       &(AstNode){.tag = astIdentifier,
                                                  .flags = flgConst,
                                                  .type = getPrimitiveType(
                                                      ctx->types, prtU64),
                                                  .ident.value = S_len})}});
        }

        // sizeof(a)/sizeof(a[0])
        return makeAstNode(
            ctx->pool,
            &node->loc,
            &(AstNode){
                .tag = astGroupExpr,
                .type = getPrimitiveType(ctx->types, prtU64),
                .groupExpr.expr = makeAstNode(
                    ctx->pool,
                    &node->loc,
                    &(AstNode){
                        .tag = astBinaryExpr,
                        .type = getPrimitiveType(ctx->types, prtU64),
                        .binaryExpr = {
                            .op = opDiv,
                            .lhs = makeSizeofNode(visitor, node, args),
                            .rhs = makeSizeofNode(
                                visitor,
                                node,
                                makeTypeinfoNode(visitor,
                                                 &node->loc,
                                                 raw->array.elementType))}})});

    case typStruct: {
        const NamedTypeMember *symbol = findStructMember(raw, S_len);
        if (symbol && nodeIs(symbol->decl, Field) &&
            isUnsignedType(symbol->type)) {
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
                                                  .flags = symbol->decl->flags,
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
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 1))
        return NULL;

    const Type *type = args->type ?: evalType(ctx, args);
    csAssert0(type);
    const Type *raw = stripAll(type);

    switch (raw->tag) {
    case typArray:
        if (isSliceType(raw)) {
            const Type *retType = makePointerType(
                ctx->types, raw->array.elementType, type->flags & flgConst);
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
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 1))
        return NULL;

    const Type *type = args->type ?: evalType(ctx, args);
    if (!isIntegralType(type) && !isFloatType(type)) {
        logError(ctx->L,
                 &args->loc,
                 "invalid `assert!` macro argument type, expecting 'bool'"
                 ", got '{t}'",
                 (FormatArg[]){{.t = type}});
    }

    AstNode *builtinAssert = findBuiltinDecl(S_CXY__builtins_assert);
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
                               .ident.value = S_CXY__builtins_assert}),
                .args = args}});
}

static AstNode *makeUncheckedNode(AstVisitor *visitor,
                                  const AstNode *node,
                                  AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 2))
        return NULL;
    AstNode *expr = args, *next = args->next;
    const Type *type = next->type;
    csAssert0(type);

    if (!typeIs(type, Info) && !hasFlag(args, Typeinfo)) {
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
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 1))
        return NULL;

    const Type *type = args->type;
    csAssert0(type);

    if (!typeIs(type, String)) {
        logError(ctx->L,
                 &args->loc,
                 "unexpected expression type passed `cstr!` macro, expecting "
                 "'string', got {t}",
                 (FormatArg[]){{.t = type}});
        return NULL;
    }

    args->type = makePointerType(
        ctx->types, getPrimitiveType(ctx->types, prtI8), flgConst);
    args->flags |= flgVisited;
    return args;
}

static AstNode *makeDestructorNode(AstVisitor *visitor,
                                   const AstNode *node,
                                   AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 1))
        return NULL;

    const Type *type = args->type ?: evalType(ctx, args);
    csAssert0(type);

    if (!typeIs(type, Info) && !hasFlag(args, Typeinfo)) {
        logError(ctx->L,
                 &args->loc,
                 "invalid `destructor!` macro parameter, expecting a "
                 "`@typeinfo`, got '{t}'",
                 (FormatArg[]){{.t = type}});
        return NULL;
    }

    args->type = makeDestructorType(ctx->types);
    args->tag = astDestructorRef;
    args->flags = flgVisited;
    memset(&args->_body, 0, CXY_AST_NODE_BODY_SIZE);
    args->destructorRef.target = type;

    return args;
}

static AstNode *makeTypeofNode(AstVisitor *visitor,
                               const AstNode *node,
                               AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 1))
        return NULL;

    const Type *type = args->type ?: evalType(ctx, args);
    csAssert0(type);

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

    return makeTypeinfoNode(visitor, &node->loc, type);
}

static AstNode *makeBaseOfNode(AstVisitor *visitor,
                               const AstNode *node,
                               AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 1))
        return NULL;

    const Type *type = args->type;
    csAssert0(type);

    if (!typeIs(type, Info) && !hasFlag(args, Typeinfo)) {
        logError(ctx->L,
                 &node->loc,
                 "invalid `typeof!` macro argument, expecting a type info "
                 "object",
                 NULL);
        return NULL;
    }

    type = type->info.target;
    if (!typeIs(type, Enum) && !typeIs(type, Class)) {
        logError(ctx->L,
                 &node->loc,
                 "invalid `typeof!` macro argument, unexpected type '{t}', "
                 "expecting a class or enum type",
                 (FormatArg[]){{.t = type}});
        return NULL;
    }

    type = getTypeBase(type) ?: makeVoidType(ctx->types);
    args->type = makeTypeInfo(ctx->types, type);
    return args;
}

static AstNode *makePointerOfNode(AstVisitor *visitor,
                                  const AstNode *node,
                                  AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
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
        args->binaryExpr.lhs->type ?: evalType(ctx, args->binaryExpr.lhs);
    csAssert0(lhs);

    if (!typeIs(lhs, Pointer)) {
        logError(ctx->L,
                 &args->binaryExpr.lhs->loc,
                 "unexpected expression passed to `ptroff`, expecting a "
                 "pointer expression",
                 NULL);
        return NULL;
    }

    const Type *rhs =
        args->binaryExpr.rhs->type ?: evalType(ctx, args->binaryExpr.rhs);
    csAssert0(rhs);

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

static int compareBuiltinMacros(const void *lhs, const void *rhs)
{
    return strcmp(((BuiltinMacro *)lhs)->name, ((BuiltinMacro *)rhs)->name);
}

static const BuiltinMacro builtinMacros[] = {
    {.name = "assert", makeAssertNode},
    {.name = "base_of", makeBaseOfNode},
    {.name = "column", makeColumnNumberNode},
    {.name = "cstr", makeCstrNode},
    {.name = "data", makeDataNode},
    {.name = "destructor", makeDestructorNode},
    {.name = "file", makeFilenameNode},
    {.name = "is_enum", makeIsEnumNode},
    {.name = "is_pointer", makeIsPointerNode},
    {.name = "is_struct", makeIsStructNode},
    {.name = "len", makeLenNode},
    {.name = "line", makeLineNumberNode},
    {.name = "mkIdent", makeAstIdentifierNode},
    {.name = "mkInteger", makeAstIntegerNode},
    {.name = "ptroff", makePointerOfNode},
    {.name = "sizeof", makeSizeofNode},
    {.name = "typeof", makeTypeofNode},
    {.name = "unchecked", makeUncheckedNode},
};

#define CXY_BUILTIN_MACROS_COUNT sizeof__(builtinMacros)

static EvaluateMacro findBuiltinMacro(cstring name)
{
    BuiltinMacro find = {.name = name};
    int index = binarySearch(builtinMacros,
                             CXY_BUILTIN_MACROS_COUNT,
                             &find,
                             sizeof(find),
                             compareBuiltinMacros);

    return index < 0 ? NULL : builtinMacros[index].eval;
}

EvaluateMacro findBuiltinMacroByNode(AstNode *node)
{
    if (nodeIs(node, Identifier))
        return findBuiltinMacro(node->ident.value);
    if (nodeIs(node, Path) && node->path.elements->next == NULL)
        return findBuiltinMacro(node->path.elements->pathElement.name);
    return NULL;
}

void evalMacroCall(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    EvaluateMacro macro = node->macroCallExpr.evaluator;
    AstNode *substitute = macro(visitor, node, node->macroCallExpr.args);
    if (!substitute) {
        node->tag = astError;
        return;
    }

    substitute->next = node->next;
    substitute->parentScope = node->parentScope;
    *node = *substitute;
}
