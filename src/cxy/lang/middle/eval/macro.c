/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-21
 */

#include "lang/middle/macro.h"
#include "lang/middle/eval/eval.h"

#include "lang/middle/builtins.h"

#include "lang/frontend/ast.h"
#include "lang/frontend/capture.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/types.h"

#include "core/alloc.h"
#include "core/sb.h"
#include "lang/middle/sema/check.h"

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
                 "unsupported number of arguments given to macro, "
                 "expecting '{u64}', got '{u64}'",
                 (FormatArg[]){{.u64 = expected}, {.u64 = count}});
        return false;
    }
    return true;
}

static void staticLog(AstVisitor *visitor,
                      DiagnosticKind lvl,
                      attr(unused) const AstNode *node,
                      attr(unused) AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (args == NULL || !nodeIs(args, StringLit)) {
        logError(ctx->L, &node->loc, "missing a message to log!", NULL);
        unreachable("ABORT COMPILE");
    }

    FormatArg *params =
        mallocOrDie(sizeof(FormatArg) * countAstNodes(args->next));
    int i = 0;
    for (AstNode *arg = args->next; arg; i++) {
        AstNode *it = arg;
        FileLoc loc = arg->loc;
        arg = arg->next;

        if (!evaluate(visitor, it)) {
            if (!nodeIs(it, Error))
                logError(ctx->L,
                         &loc,
                         "unable evaluate argument at compile time",
                         NULL);
            goto staticLogDone;
        }

        switch (it->tag) {
        case astStringLit:
            params[i] = (FormatArg){.s = it->stringLiteral.value};
            break;
        case astIntegerLit:
            if (it->intLiteral.isNegative)
                params[i] = (FormatArg){.i64 = it->intLiteral.value};
            else
                params[i] = (FormatArg){.u64 = it->intLiteral.uValue};
            break;
        case astFloatLit:
            params[i] = (FormatArg){.f64 = it->floatLiteral.value};
            break;
        case astCharLit:
            params[i] = (FormatArg){.c = it->charLiteral.value};
            break;
        case astBoolLit:
            params[i] = (FormatArg){.b = it->boolLiteral.value};
            break;
        case astTypeRef:
            params[i] = (FormatArg){.t = it->type};
            break;
        default:
            if (isTypeExpr(it))
                params[i] = (FormatArg){.t = it->type ?: evalType(ctx, it)};
            else
                unreachable("ABORT COMPILE");
            break;
        }
    }

    switch (lvl) {
    case dkNote:
        logNote(ctx->L, &node->loc, args->stringLiteral.value, params);
        break;
    case dkError:
        logError(ctx->L, &node->loc, args->stringLiteral.value, params);
        break;
    case dkWarning:
        logWarning(ctx->L, &node->loc, args->stringLiteral.value, params);
        break;
    }
staticLogDone:
    free(params);
}

static AstNode *makeAstLogErrorNode(AstVisitor *visitor,
                                    attr(unused) const AstNode *node,
                                    attr(unused) AstNode *args)
{
    staticLog(visitor, dkError, node, args);
    return NULL;
}

static AstNode *makeAstLogWarningNode(AstVisitor *visitor,
                                      attr(unused) const AstNode *node,
                                      attr(unused) AstNode *args)
{
    staticLog(visitor, dkWarning, node, args);
    args->tag = astNoop;
    return args;
}

static AstNode *makeAstLogNoteNode(AstVisitor *visitor,
                                   attr(unused) const AstNode *node,
                                   attr(unused) AstNode *args)
{
    staticLog(visitor, dkNote, node, args);
    args->tag = astNoop;
    return args;
}

static AstNode *makeRequireNode(AstVisitor *visitor,
                                attr(unused) const AstNode *node,
                                attr(unused) AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (args == NULL) {
        logError(ctx->L,
                 &node->loc,
                 "call to `require!` macro is missing condition argument",
                 NULL);
        return NULL;
    }
    AstNode *cond = args;
    FileLoc loc = cond->loc;
    args = args->next;
    if (!evaluate(visitor, cond)) {
        if (!nodeIs(cond, Error))
            logError(ctx->L,
                     &loc,
                     "condition argument of `require!` macro must be comptime "
                     "evaluable",
                     NULL);
        return NULL;
    }

    if (!nodeIs(cond, BoolLit)) {
        logError(ctx->L,
                 &loc,
                 "condition argument of `require!` macro must evaluate to a "
                 "boolean literal",
                 NULL);
        return NULL;
    }

    if (!cond->boolLiteral.value) {
        staticLog(visitor, dkError, node, args);
        args->tag = astError;
    }
    else {
        args->tag = astNoop;
    }
    args->next = NULL;
    return args;
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

static AstNode *makeCopyNode(AstVisitor *visitor,
                             attr(unused) const AstNode *node,
                             attr(unused) AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 1))
        return NULL;
    const Type *type = args->type ?: evalType(ctx, args);
    if (typeIs(type, Error))
        return NULL;
    if (!nodeIsLeftValue(args)) {
        logError(ctx->L, &args->loc, "expecting a lvalue expression", NULL);
        return NULL;
    }
    if (isReferenceType(type))
        type = type->reference.referred;
    return makeBackendCallExpr(
        ctx->pool, &node->loc, flgNone, bfiCopy, args, type);
}

static AstNode *makeForceDropNode(AstVisitor *visitor,
                                  attr(unused) const AstNode *node,
                                  attr(unused) AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 1))
        return NULL;
    const Type *type = args->type ?: evalType(ctx, args);
    if (typeIs(type, Error))
        return NULL;
    if (!nodeIsLeftValue(args)) {
        logError(ctx->L, &args->loc, "expecting a lvalue expression", NULL);
        return NULL;
    }

    return makeBackendCallExpr(
        ctx->pool, &node->loc, flgForced, bfiDrop, args, type);
}

static AstNode *makeExNode(AstVisitor *visitor,
                           attr(unused) const AstNode *node,
                           attr(unused) AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    TypingContext *typer = getAstVisitorContext(ctx->typer);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 0))
        return NULL;
    if (typer->catcher.block == NULL) {
        logError(ctx->L,
                 &node->loc,
                 "`ex!` macro only allowed in catch blocks",
                 NULL);
        return NULL;
    }

    if (typer->catcher.result == NULL) {
        AstNode *lhs = typer->catcher.expr;
        const Type *type = resolveUnThisUnwrapType(lhs->type);
        typer->catcher.result = makeVarDecl(
            ctx->pool,
            &node->loc,
            flgNone,
            makeAnonymousVariable(ctx->strings, "_res"),
            NULL,
            isVoidResultType(type) ? lhs : shallowCloneAstNode(ctx->pool, lhs),
            NULL,
            type);
        AstNode *ex = makeCastResultTo(typer, typer->catcher.result, true);
        typer->catcher.ex =
            makeVarDecl(ctx->pool,
                        builtinLoc(),
                        flgNone,
                        makeAnonymousVariable(ctx->strings, "_ex"),
                        NULL,
                        ex,
                        NULL,
                        ex->type);
    }

    return makeResolvedPath(ctx->pool,
                            &node->loc,
                            typer->catcher.ex->_name,
                            flgNone,
                            typer->catcher.ex,
                            NULL,
                            typer->catcher.ex->type);
}

static AstNode *makeHasMemberNode(AstVisitor *visitor,
                                  attr(unused) const AstNode *node,
                                  attr(unused) AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 3))
        return NULL;

    const Type *target = args->type ?: evalType(ctx, args);
    if (!nodeIs(args, TypeRef) && !hasFlag(args, Typeinfo)) {
        logError(ctx->L,
                 &args->loc,
                 "unexpected type {t}, expecting type information",
                 (FormatArg[]){{.t = target}});
        return NULL;
    }
    target = resolveType(target);

    AstNode *name = args->next, *memberType = name->next;
    if (!evaluate(visitor, name))
        return NULL;
    if (!nodeIs(name, StringLit)) {
        logError(
            ctx->L,
            &name->loc,
            "unexpect argument, expecting the name of the member to lookup",
            NULL);
        return NULL;
    }

    const Type *type = evalType(ctx, memberType);
    if (!hasFlag(memberType, Typeinfo)) {
        logError(ctx->L,
                 &memberType->loc,
                 "unexpected type {t}, expecting type information",
                 (FormatArg[]){{.t = type}});
        return NULL;
    }

    type = resolveType(type);
    args->next = NULL;
    args->tag = astBoolLit;

    const Type *member = findMemberInType(target, name->stringLiteral.value);
    if (typeIs(member, Func) && typeIs(type, Func)) {
        member = matchOverloadedFunctionPerfectMatch(ctx->L,
                                                     member,
                                                     type->func.params,
                                                     type->func.paramsCount,
                                                     NULL,
                                                     type->flags & flgConst,
                                                     true);
        args->boolLiteral.value =
            member && compareFuncTypes(member, type, true);
    }
    else {
        args->boolLiteral.value = member && compareTypes(member, type);
    }
    return args;
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
        &node->loc,
        &(AstNode){.tag = astIntegerLit,
                   .type = getPrimitiveType(ctx->types, prtU64),
                   .intLiteral.uValue = visitor->current->loc.begin.row});
}

static AstNode *makeOffsetNumberNode(AstVisitor *visitor,
                                     attr(unused) const AstNode *node,
                                     attr(unused) AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 0))
        return NULL;

    return makeAstNode(
        ctx->pool,
        &node->loc,
        &(AstNode){.tag = astIntegerLit,
                   .type = getPrimitiveType(ctx->types, prtU64),
                   .intLiteral.uValue = visitor->current->loc.begin.row});
}

static AstNode *lShift(MemPool *P, AstNode *left, AstNode *node)
{
    if (nodeIs(node, BinaryExpr) && node->binaryExpr.op == opShl) {
        AstNode *lhs = node->binaryExpr.lhs, *right = node->binaryExpr.rhs;
        return makeBinaryExpr(P,
                              &node->loc,
                              flgNone,
                              lShift(P, left, lhs),
                              opShl,
                              right,
                              NULL,
                              NULL);
    }
    return makeBinaryExpr(
        P, &node->loc, flgNone, left, opShl, node, NULL, NULL);
}

static AstNode *makeLShiftNode(AstVisitor *visitor,
                               attr(unused) const AstNode *node,
                               attr(unused) AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 2))
        return NULL;

    AstNode *left = args, *right = args->next;
    if (nodeIs(right, BinaryExpr) && right->binaryExpr.op == opShl) {
        return lShift(ctx->pool, left, right);
    }
    return makeBinaryExpr(
        ctx->pool, &node->loc, flgNone, left, opShl, right, NULL, NULL);
}

static AstNode *makeAstNodeList(AstVisitor *visitor,
                                attr(unused) const AstNode *node,
                                attr(unused) AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 0))
        return NULL;

    return makeAstNode(ctx->pool,
                       &node->loc,
                       &(AstNode){.tag = astList,
                                  .flags = flgComptime,
                                  .type = makeAutoType(ctx->types)});
}

static AstNode *actionAstListAdd(AstVisitor *visitor,
                                 attr(unused) const AstNode *node,
                                 attr(unused) AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 2))
        return NULL;
    AstNode *list = args, *value = args->next;
    AstNode *resolved = resolvePath(list);
    if (!nodeIs(resolved, VarDecl) && !nodeIs(resolved->varDecl.init, List)) {
        logError(ctx->L,
                 &list->loc,
                 "invalid argument passed to `make_struct_expr!` expecting a "
                 "list of field expressions",
                 NULL);
        return NULL;
    }

    const Type *type = value->type ?: evalType(ctx, value);
    csAssert0(type);

    insertAstNode(&resolved->varDecl.init->nodesList.nodes, value);

    args->next = NULL;
    args->tag = astNoop;
    return args;
}

static AstNode *makeAstPasteNode(AstVisitor *visitor,
                                 attr(unused) const AstNode *node,
                                 attr(unused) AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 1))
        return NULL;
    AstNode *list = args;
    AstNode *resolved = resolvePath(list);
    if (!nodeIs(resolved, VarDecl) || !hasFlag(resolved, Comptime)) {
        logError(ctx->L,
                 &list->loc,
                 "invalid argument passed to `ast_paste!` expecting a "
                 "reference to a compile time variable",
                 NULL);
        return NULL;
    }

    return deepCloneAstNode(ctx->pool, resolved->varDecl.init);
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
        &node->loc,
        &(AstNode){.tag = astIntegerLit,
                   .type = getPrimitiveType(ctx->types, prtU64),
                   .intLiteral.uValue = visitor->current->loc.begin.col});
}

static AstNode *makeSizeofNode(AstVisitor *visitor,
                               attr(unused) const AstNode *node,
                               attr(unused) AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 1))
        return NULL;

    const Type *type = args->type ?: evalType(ctx, args);
    csAssert0(type);
    args->tag = astTypeRef;
    args->type = makeTypeInfo(ctx->types, type);

    return makeBackendCallExpr(ctx->pool,
                               &node->loc,
                               flgNone,
                               bfiSizeOf,
                               args,
                               getPrimitiveType(ctx->types, prtU64));
}

static AstNode *makeAstFieldStmtNode(AstVisitor *visitor,
                                     attr(unused) const AstNode *node,
                                     attr(unused) AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 2))
        return NULL;
    u64 count = countAstNodes(args);
    if (count < 2 || count > 3) {
        logError(ctx->L,
                 &node->loc,
                 "unsupported number of arguments given to macro, mk_field!"
                 "expecting at least 2 and less than 3, got '{u64}'",
                 (FormatArg[]){{.u64 = count}});
        return false;
    }
    FileLoc loc = args->loc;
    AstNode *name = args, *type = args->next, *value = type->next;
    args->next = type->next = NULL;
    if (!evaluate(visitor, name))
        return NULL;
    if (!nodeIs(name, StringLit) && !nodeIs(name, Identifier)) {
        logError(ctx->L,
                 &loc,
                 "invalid argument passed to `make_field!` expecting a "
                 "string literal or an identifier",
                 NULL);
        return NULL;
    }

    args->tag = astFieldDecl;
    args->structField.name = nodeIs(name, Identifier)
                                 ? name->ident.value
                                 : name->stringLiteral.value;
    args->structField.value = value;
    args->structField.type = type;
    args->flags |= flgVisited;
    args->type = evalType(ctx, args);

    if (typeIs(args->type, Error))
        return NULL;

    return args;
}

static AstNode *makeAstFieldExprNode(AstVisitor *visitor,
                                     attr(unused) const AstNode *node,
                                     attr(unused) AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 2))
        return NULL;
    FileLoc loc = args->loc;
    AstNode *name = args, *value = args->next;
    if (!evaluate(visitor, name))
        return NULL;
    if (!nodeIs(name, StringLit) && !nodeIs(name, Identifier)) {
        logError(ctx->L,
                 &loc,
                 "invalid argument passed to `make_field_expr!` expecting a "
                 "string literal",
                 NULL);
        return NULL;
    }

    const Type *type = value->type ?: evalType(ctx, value);
    if (typeIs(type, Error))
        return NULL;

    args->tag = astFieldExpr;
    args->fieldExpr.name = nodeIs(name, Identifier) ? name->ident.value
                                                    : name->stringLiteral.value;
    args->fieldExpr.value = value;
    args->type = value->type;
    args->flags |= flgVisited;

    return args;
}

static AstNode *makeAstStructExprNode(AstVisitor *visitor,
                                      attr(unused) const AstNode *node,
                                      attr(unused) AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 2))
        return NULL;
    FileLoc loc = args->loc;
    AstNode *left = args, *value = args->next;
    if (!evaluate(visitor, left))
        return NULL;
    if (!nodeIs(left, TypeRef) && !nodeIs(left, Ref)) {
        logError(ctx->L,
                 &loc,
                 "invalid argument passed to `make_struct_expr!` expecting a "
                 "type object",
                 NULL);
        return NULL;
    }
    left->next = NULL;

    loc = value->loc;
    AstNode *resolved = resolvePath(value);
    if (!nodeIs(resolved, VarDecl) && !nodeIs(resolved->varDecl.init, List)) {
        logError(ctx->L,
                 &loc,
                 "invalid argument passed to `make_struct_expr!` expecting a "
                 "list of field expressions",
                 NULL);
        return NULL;
    }
    AstNode *fields = resolved->varDecl.init->nodesList.nodes.first;

    value->tag = astStructExpr;
    value->structExpr.left = left;
    value->structExpr.fields = fields;
    value->type = NULL;
    value->flags = flgNone;

    return value;
}

static AstNode *makeAstTupleExprNode(AstVisitor *visitor,
                                     attr(unused) const AstNode *node,
                                     attr(unused) AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 1))
        return NULL;

    AstNode *list = args;
    AstNode *resolved = resolvePath(list);
    if (!nodeIs(resolved, VarDecl) && !nodeIs(resolved->varDecl.init, List)) {
        logError(ctx->L,
                 &list->loc,
                 "invalid argument passed to `make_tuple_expr!` expecting a "
                 "list of field expressions",
                 NULL);
        return NULL;
    }

    args->tag = astTupleExpr;
    args->tupleExpr.elements = resolved->varDecl.init->nodesList.nodes.first;
    args->flags = flgNone;
    args->type = NULL;

    return args;
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
    args->type = makeAutoType(ctx->types);
    return args;
}

static AstNode *makeAstStringNode(AstVisitor *visitor,
                                  attr(unused) const AstNode *node,
                                  attr(unused) AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (args == NULL) {
        logError(ctx->L,
                 &node->loc,
                 "invalid number of arguments passed to `mk_str!` macro, "
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
    args->tag = astStringLit;
    args->stringLiteral.value = makeString(ctx->strings, str);
    free(str);
    args->type = makeStringType(ctx->types);
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

static AstNode *makeInitializeDefaults(AstVisitor *visitor,
                                       attr(unused) const AstNode *node,
                                       attr(unused) AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 1))
        return NULL;

    const Type *type = args->type ?: evalType(ctx, args);
    csAssert0(type);
    const Type *raw = stripAll(type);
    AstNode *init = findMemberDeclInType(raw, S___defaults_init);
    if (init == NULL) {
        args->tag = astNoop;
    }
    else {
        return makeExprStmt(
            ctx->pool,
            &args->loc,
            flgNone,
            makeCallExpr(ctx->pool,
                         &args->loc,
                         makeMemberExpr(ctx->pool,
                                        &args->loc,
                                        flgNone,
                                        args,
                                        makeResolvedIdentifier(ctx->pool,
                                                               &node->loc,
                                                               init->_name,
                                                               0,
                                                               init,
                                                               NULL,
                                                               init->type),
                                        NULL,
                                        init->type),
                         NULL,
                         flgNone,
                         NULL,
                         makeVoidType(ctx->types)),
            NULL,
            makeVoidType(ctx->types));
    }
    return args;
}

static AstNode *makeLambdaOfAstNode(AstVisitor *visitor,
                                    attr(unused) const AstNode *node,
                                    attr(unused) AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 1))
        return NULL;

    const Type *func = resolveType(args->type ?: evalType(ctx, args));
    if (!typeIs(func, Func)) {
        logError(ctx->L,
                 &args->loc,
                 "`lambda_of!` macro expecting a function type, given '{t}'",
                 (FormatArg[]){{.t = func}});
        return NULL;
    }
    AstNode *decl = deepCloneAstNode(ctx->pool, func->func.decl);
    AstNode *params = decl->funcType.params;
    params = makeFunctionParam(
        ctx->pool,
        &args->loc,
        makeString(ctx->strings, "_"),
        makeTypeReferenceNode(
            ctx->pool, makeVoidPointerType(ctx->types, flgNone), &args->loc),
        NULL,
        flgNone,
        params);
    decl->funcType.params = params;
    decl->type = NULL;
    func = evalType(ctx, decl);
    csAssert0(typeIs(func, Func));

    return makeTupleTypeAst(
        ctx->pool,
        &decl->loc,
        flgTypeinfo | flgComptime,
        makeTypeReferenceNode2(ctx->pool,
                               makeVoidPointerType(ctx->types, flgNone),
                               &decl->loc,
                               decl),
        NULL,
        makeTupleType(
            ctx->types,
            (const Type *[]){makeVoidPointerType(ctx->types, flgNone), func},
            2,
            flgFuncTypeParam));
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

static AstNode *makeBackendCallNode(AstVisitor *visitor,
                                    const AstNode *node,
                                    AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    u64 count = args ? countAstNodes(args) : 0;
    if (count < 2) {
        logError(ctx->L,
                 &node->loc,
                 "unsupported number of arguments given to macro 'mk_bc!', "
                 "expecting at least 2 arguments, got '{u64}'",
                 (FormatArg[]){{.u64 = count}});
        return NULL;
    }
    if (!nodeIs(args, IntegerLit)) {
        logError(ctx->L,
                 &node->loc,
                 "the first argument of `mk_bc!` must be the ID of the API",
                 NULL);
        return NULL;
    }
    if (!hasFlag(args->next, Typeinfo)) {
        logError(ctx->L,
                 &node->loc,
                 "the second argument of `mk_bc!` must be the return type of "
                 "the call",
                 NULL);
        return NULL;
    }

    i64 id = args->intLiteral.value;
    AstNode *arg = args->next;
    for (; arg; arg = arg->next) {
        const Type *type = arg->type ?: evalType(ctx, arg);
        if (type == NULL || typeIs(type, Error))
            return NULL;
    }

    arg = args->next;
    args->tag = astBackendCall;
    args->backendCallExpr.func = id;
    args->backendCallExpr.args = arg->next;
    args->type = arg->type;
    args->next = NULL;
    return args;
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
            args->intLiteral.isNegative = false;
            args->type = getPrimitiveType(ctx->types, prtU64);
            u64 len = strlen(args->stringLiteral.value);
            memset(&args->stringLiteral, 0, sizeof(args->stringLiteral));
            args->intLiteral.uValue = len;
            return args;
        }
        AstNode *strLen = findBuiltinDecl(S_strlen);
        csAssert0(strLen);
        return makeCallExpr(ctx->pool,
                            &node->loc,
                            makeResolvedPath(ctx->pool,
                                             &node->loc,
                                             S_strlen,
                                             strLen->flags | node->flags,
                                             strLen,
                                             NULL,
                                             strLen->type),
                            args,
                            node->flags,
                            NULL,
                            strLen->type->func.retType);
    }
    case typArray:
        // sizeof(a)/sizeof(a[0])
        args->tag = astIntegerLit;
        args->intLiteral.isNegative = false;
        args->type = getPrimitiveType(ctx->types, prtU64);
        clearAstBody(args);
        args->intLiteral.uValue = raw->array.len;
        return args;

    case typStruct: {
        const NamedTypeMember *symbol = findStructMember(raw, S_len);
        if (symbol && nodeIs(symbol->decl, FieldDecl) &&
            isUnsignedType(symbol->type)) {
            return makeMemberExpr(
                ctx->pool,
                &node->loc,
                args->flags,
                args,
                makeResolvedIdentifier(ctx->pool,
                                       &args->loc,
                                       S_len,
                                       0,
                                       (AstNode *)symbol->decl,
                                       NULL,
                                       symbol->type),
                NULL,
                symbol->type);
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

    AstNode *builtinAssert = findBuiltinDecl(S___cxy_assert);
    csAssert0(builtinAssert);
    AstNode *next = args;
    next = next->next = makeFilenameNode(visitor, node, NULL);
    next = next->next = makeLineNumberNode(visitor, node, NULL);
    next->next = makeColumnNumberNode(visitor, node, NULL);

    return makeCallExpr(ctx->pool,
                        &node->loc,
                        makeResolvedIdentifier(ctx->pool,
                                               &node->loc,
                                               S___cxy_assert,
                                               0,
                                               builtinAssert,
                                               NULL,
                                               builtinAssert->type),
                        args,
                        node->flags,
                        NULL,
                        builtinAssert->type->func.retType);
}

static AstNode *makeUncheckedNode(AstVisitor *visitor,
                                  const AstNode *node,
                                  AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 2))
        return NULL;
    AstNode *expr = args, *next = args->next;
    const Type *type = next->type ?: evalType(ctx, next);
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
                                      &(AstNode){.tag = astNoop,
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
    if (args == NULL || !validateMacroArgumentCount(ctx, &node->loc, args, 1))
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

static AstNode *makeTypeAtIdxNode(AstVisitor *visitor,
                                  const AstNode *node,
                                  AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 2))
        return NULL;
    AstNode *index = args->next;
    const FileLoc *loc = &index->loc;

    const Type *type = args->type ?: evalType(ctx, args);
    csAssert0(type);
    type = resolveType(unwrapType(type, NULL));

    if (!evaluate(visitor, index) || !nodeIs(index, IntegerLit)) {
        logError(ctx->L,
                 loc,
                 "invalid `typeat!` macro argument, second argument must be "
                 "compile time integer literal",
                 NULL);
        return NULL;
    }

    u64 idx = integerLiteralValue(index);
    const Type *atIndex = NULL;
    switch (type->tag) {
    case typTuple:
        if (idx < type->tuple.count) {
            atIndex = type->tuple.members[idx];
            break;
        }

        logError(ctx->L,
                 loc,
                 "invalid `typeat!` macro argument, index {u64} out of "
                 "bounds on type {t}",
                 (FormatArg[]){{.u64 = idx}, {.t = type}});
        return NULL;
    case typUnion:
        if (idx < type->tUnion.count) {
            atIndex = type->tUnion.members[idx].type;
            break;
        }

        logError(ctx->L,
                 loc,
                 "invalid `typeat!` macro argument, index {u64} out of "
                 "bounds on type {t}",
                 (FormatArg[]){{.u64 = idx}, {.t = type}});
        return NULL;
    default:
        logError(
            ctx->L,
            &node->loc,
            "invalid `typeat!` macro argument, type {t} does not have members",
            (FormatArg[]){{.t = type}});
        return NULL;
    }

    return makeTypeReferenceNode(ctx->pool, atIndex, &node->loc);
}

static AstNode *makeIndexOfNode(AstVisitor *visitor,
                                const AstNode *node,
                                AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 2))
        return NULL;

    AstNode *index = args->next;
    const FileLoc *loc = &index->loc;

    const Type *type = args->type ?: evalType(ctx, args);
    if (typeIs(type, Error))
        return NULL;
    csAssert0(type);
    type = resolveType(unwrapType(type, NULL));

    const Type *target = index->type ?: evalType(ctx, index);
    if (typeIs(target, Error))
        return NULL;
    csAssert0(target);

    i64 idx = -1;
    switch (type->tag) {
    case typTuple:
        for (int i = 0; i < type->tuple.count; i++) {
            if (compareTypes(type->tuple.members[i], target)) {
                idx = i;
                break;
            }
        }
        break;
    case typUnion:
        for (int i = 0; i < type->tUnion.count; i++) {
            if (compareTypes(type->tUnion.members[i].type, target)) {
                idx = i;
                break;
            }
        }
        break;
    default:
        logError(
            ctx->L,
            &node->loc,
            "invalid `indexof!` macro argument, type {t} does not have members",
            (FormatArg[]){{.t = type}});
        return NULL;
    }

    return makeIntegerLiteral(
        ctx->pool, &node->loc, idx, NULL, getPrimitiveType(ctx->types, prtI64));
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

static AstNode *makeIsBaseOfNode(AstVisitor *visitor,
                                 const AstNode *node,
                                 AstNode *args)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!validateMacroArgumentCount(ctx, &node->loc, args, 2))
        return NULL;

    const Type *base = args->type ?: evalType(ctx, args);
    const Type *type = args->next->type ?: evalType(ctx, args->next);

    if (typeIs(base, Error) || typeIs(type, Error))
        return NULL;

    if (!typeIs(base, Info) && !hasFlag(args, Typeinfo)) {
        logError(ctx->L,
                 &args->loc,
                 "invalid `is_base_of!` macro 'base' argument, expecting a "
                 "type info object",
                 NULL);
        return NULL;
    }
    if (!typeIs(type, Info) && !hasFlag(args->next, Typeinfo)) {
        logError(ctx->L,
                 &args->next->loc,
                 "invalid `is_base_of!` macro 'type' argument, expecting a "
                 "type info object",
                 NULL);
        return NULL;
    }
    base = resolveUnThisUnwrapType(base);
    type = resolveUnThisUnwrapType(type);
    const Type *impl = getTypeBase(type);
    args->tag = astBoolLit;
    args->next = NULL;
    args->type = getPrimitiveType(ctx->types, prtBool);
    clearAstBody(args);
    args->boolLiteral.value = impl && impl == base;
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

/**
 * @note this list is in sorted order by name, optimizing for
 * binary search (keep the same order)... might have to change to a hash-table,
 * or a trie, but with these minimal entries, it's fast enough.
 */
static const BuiltinMacro builtinMacros[] = {
    {.name = "__copy", makeCopyNode},
    {.name = "__forcedrop", makeForceDropNode},
    {.name = "assert", makeAssertNode},
    {.name = "ast_list_add", actionAstListAdd},
    {.name = "ast_paste", makeAstPasteNode},
    {.name = "base_of", makeBaseOfNode},
    {.name = "column", makeColumnNumberNode},
    {.name = "cstr", makeCstrNode},
    {.name = "data", makeDataNode},
    {.name = "destructor", makeDestructorNode},
    {.name = "error", makeAstLogErrorNode},
    {.name = "ex", makeExNode},
    {.name = "file", makeFilenameNode},
    {.name = "has_member", makeHasMemberNode},
    {.name = "indexof", makeIndexOfNode},
    {.name = "info", makeAstLogNoteNode},
    {.name = "init_defaults", makeInitializeDefaults},
    {.name = "is_base_of", makeIsBaseOfNode},
    {.name = "lambda_of", makeLambdaOfAstNode},
    {.name = "len", makeLenNode},
    {.name = "line", makeLineNumberNode},
    {.name = "lshift", makeLShiftNode},
    {.name = "mk_ast_list", makeAstNodeList},
    {.name = "mk_bc", makeBackendCallNode},
    {.name = "mk_field", makeAstFieldStmtNode},
    {.name = "mk_field_expr", makeAstFieldExprNode},
    {.name = "mk_ident", makeAstIdentifierNode},
    {.name = "mk_integer", makeAstIntegerNode},
    {.name = "mk_str", makeAstStringNode},
    {.name = "mk_struct_expr", makeAstStructExprNode},
    {.name = "mk_tuple_expr", makeAstTupleExprNode},
    {.name = "offset", makeOffsetNumberNode},
    {.name = "ptroff", makePointerOfNode},
    {.name = "require", makeRequireNode},
    {.name = "sizeof", makeSizeofNode},
    {.name = "typeat", makeTypeAtIdxNode},
    {.name = "typeof", makeTypeofNode},
    {.name = "unchecked", makeUncheckedNode},
    {.name = "warn", makeAstLogWarningNode},
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
