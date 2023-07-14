/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-17
 */

#include "semantics.h"

#include "eval.h"
#include "flag.h"
#include "visitor.h"

#include "lang/ttable.h"

static void checkProgram(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    cstring namespace = ctx->typeTable->currentNamespace;
    initializeModule(visitor, node);
    if (ctx->isBuiltins) {
        initializeBuiltins(ctx);
    }

    if (node->program.top)
        checkMany(visitor, node->program.top);

    ctx->previousTopLevelDecl = node->program.decls;
    for (AstNode *decl = node->program.decls; decl; decl = decl->next) {
        ctx->currentTopLevelDecl = decl;
        decl->parentScope = node;
        astVisit(visitor, decl);
        ctx->previousTopLevelDecl = decl;
    }

    finalizeModule(visitor, node, namespace);
}

static void checkFallback(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    switch (node->tag) {
    case astExprStmt:
        node->type = evalType(visitor, node->exprStmt.expr);
        break;
    case astStmtExpr:
        node->type = evalType(visitor, node->stmtExpr.stmt);
        if (typeIs(node->type, Void) &&
            nodeIs(node->stmtExpr.stmt, BlockStmt) &&
            node->stmtExpr.stmt->blockStmt.last) {
            node->type = node->stmtExpr.stmt->blockStmt.last->type;
        }
        break;
    case astGroupExpr:
        node->type = evalType(visitor, node->groupExpr.expr);
        break;
    default:
        node->type = makeVoidType(ctx->typeTable);
    }
}

static void checkIdentifier(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *symbol =
        findSymbol(ctx->env, ctx->L, node->ident.value, &node->loc);
    if (symbol == NULL)
        node->type = ERROR_TYPE(ctx);
    else
        node->type = symbol->type;
}

static void checkDeferStmt(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    if (node->parentScope == NULL || node->parentScope->tag != astBlockStmt) {
        logError(ctx->L,
                 &node->loc,
                 "use of 'defer' statement outside of a block",
                 NULL);
    }

    node->type = evalType(visitor, node->deferStmt.expr);
}

static void checkBreakContinueStmt(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    if (nodeIs(node, BreakStmt))
        findEnclosingLoopOrSwitch(ctx->env, ctx->L, "break", &node->loc);
    else
        findEnclosingLoop(ctx->env, ctx->L, "continue", &node->loc);

    node->type = makeVoidType(ctx->typeTable);
}

static void semanticsDispatch(Visitor func, AstVisitor *visitor, AstNode *node)
{
    if (hasFlag(node, Comptime))
        checkComptime(visitor, node);
    else
        func(visitor, node);
}

static SymbolRef *findSymbolRefByPath(const Env *env,
                                      Log *L,
                                      const AstNode *node)
{
    AstNode *elem = node->path.elements;
    do {
        SymbolRef *ref =
            findSymbolRef(env, L, elem->pathElement.name, &elem->loc);
        if (elem->next == NULL || ref == NULL)
            return ref;

        const Type *type = stripAll(ref->node->type);
        elem = elem->next;

        switch (type->tag) {
        case typEnum:
            env = type->tEnum.decl->env;
            break;
        case typStruct:
            env = type->tStruct.decl->env;
            break;
        case typContainer:
            env = type->container.decl->env;
            break;
        case typModule:
            env = ref->node->env;
            break;
        default:
            if (L)
                logError(L,
                         &elem->loc,
                         "type '{t}' does not support member syntax",
                         (FormatArg[]){{.t = type}});
            return NULL;
        }
    } while (true);
}

static SymbolRef *findSymbolRefMemberExpr(const Env *env,
                                          Log *L,
                                          const AstNode *node)
{
    if (!nodeIs(node->memberExpr.member, Identifier))
        return NULL;

    AstNode *target = node->memberExpr.target;
    SymbolRef *ref = findSymbolRefByNode(env, L, target);
    if (ref == NULL)
        return ref;

    const Type *type = stripAll(ref->node->type);
    switch (ref->node->tag) {
    case astEnumDecl:
    case astStructDecl:
    case astImportDecl:
    case astModuleDecl:
    case astDefine:
        env = ref->node->env;
        break;

    default:
        logError(L,
                 &target->loc,
                 "type '{t}' does not support member syntax",
                 (FormatArg[]){{.t = type}});
        return NULL;
    }

    return findSymbolRefByNode(env, L, node->memberExpr.member);
}

attr(always_inline) static AstNode *findSymbolOnlyByPath(const Env *env,
                                                         const AstNode *node)
{
    SymbolRef *ref = findSymbolRefByPath(env, NULL, node);
    return ref ? ref->node : NULL;
}

const Type *evalType(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    astVisit(visitor, node);
    const Type *type = resolveType(node->type);
    if (hasFlag(node, Typeinfo))
        return node->type = makeTypeInfo(ctx->typeTable, node->type);
    return type;
}

u64 checkMany(AstVisitor *visitor, AstNode *node)
{
    u64 i = 0;
    for (; node; node = node->next, i++)
        astVisit(visitor, node);

    return i;
}

void addTopLevelDecl(SemanticsContext *ctx, cstring name, AstNode *node)
{
    if (name) {
        Env *env = environmentRoot(ctx->env);
        if (!defineSymbol(env, ctx->L, name, node))
            return;
    }

    node->flags |= flgTopLevelDecl;
    if (ctx->previousTopLevelDecl == ctx->currentTopLevelDecl) {
        ctx->program->program.decls = node;
        node->next = ctx->currentTopLevelDecl;
    }
    else {
        ctx->previousTopLevelDecl->next = node;
        node->next = ctx->currentTopLevelDecl;
    }
    ctx->previousTopLevelDecl = node;
}

cstring getDeclarationAlias(SemanticsContext *ctx, AstNode *node)
{
    const AstNode *alias = findAttribute(node, "alias");
    if (alias == NULL)
        return NULL;

    if (!hasFlag(node, Native)) {
        logWarning(ctx->L,
                   &alias->loc,
                   "`@alias` attribute can only be used with a public native "
                   "declaration",
                   NULL);
        return NULL;
    }

    const AstNode *name = findAttributeArgument(alias, "name");
    if (name == NULL || !nodeIs(name, StringLit)) {
        logWarning(ctx->L,
                   &alias->loc,
                   "`@alias` accepts a name which must be provided in the "
                   "format `name: "
                   "\"AliasName\"",
                   NULL);
        return NULL;
    }

    return name->stringLiteral.value;
}

AstNode *findSymbolByPath(const Env *env, Log *L, const AstNode *node)
{
    SymbolRef *ref = findSymbolRefByPath(env, L, node);
    return ref ? ref->node : NULL;
}

AstNode *findSymbolByNode(const Env *env, Log *L, const AstNode *node)
{
    switch (node->tag) {
    case astPath:
        return findSymbolByPath(env, L, node);
    case astIdentifier:
        return findSymbol(env, L, node->ident.value, &node->loc);
    default:
        return NULL;
    }
}

SymbolRef *findSymbolRefByNode(const Env *env, Log *L, const AstNode *node)
{
    switch (node->tag) {
    case astPath:
        return findSymbolRefByPath(env, L, node);
    case astIdentifier:
        return findSymbolRef(env, L, node->ident.value, &node->loc);
    case astMemberExpr:
        return findSymbolRefMemberExpr(env, L, node);
    default:
        return NULL;
    }
}

AstNode *findFunctionWithSignature(SemanticsContext *ctx,
                                   const Env *env,
                                   cstring name,
                                   u64 flags,
                                   const Type **params,
                                   u64 paramsCount)
{
    SymbolRef *ref = findSymbolRef(env, NULL, name, NULL);
    return ref ? symbolRefLookupFuncDeclBySignature(
                     ctx, ref, flags, params, paramsCount, NULL, true)
               : NULL;
}

AstNode *symbolRefLookupFuncDeclBySignature(SemanticsContext *ctx,
                                            SymbolRef *decls,
                                            u64 flags,
                                            const Type **params,
                                            u64 paramsCount,
                                            const FileLoc *loc,
                                            bool constructible)
{
    if (decls == NULL)
        return NULL;

    if (!nodeIs(decls->node, FuncDecl)) {
        if (loc) {
            logError(ctx->L,
                     loc,
                     "unexpected variable, expecting a function declaration",
                     NULL);
            logNote(ctx->L,
                    &decls->node->loc,
                    "is not a function declaration",
                    NULL);
        }
        return NULL;
    }

    SymbolRef *it = decls;
    AstNode *match = NULL;
    u64 declarations = 0, maxScore = paramsCount * 2, matchScore = 0;
    while (it) {
        declarations++;
        AstNode *decl = it->node;
        it = it->next;

        const Type *type = decl->type;
        if (type == NULL)
            continue;

        const u64 requiredParamsCount =
            type->func.paramsCount - type->func.defaultValuesCount;

        if ((decl->flags & flags) != flags)
            continue;
        if (type->func.paramsCount < paramsCount ||
            paramsCount < requiredParamsCount)
            continue;

        bool compatible = true;
        u64 score = maxScore, i = (decl->flags & flgClosureStyle) ? 1 : 0;
        for (; i < paramsCount; i++) {
            compatible = isTypeAssignableFrom(type->func.params[i], params[i]);
            if (!compatible) {
                if (constructible && nodeIs(decl->parentScope, StructDecl)) {
                    score--;
                    compatible = isExplicitConstructibleFrom(
                        ctx, type->func.params[i], params[i]);
                    if (!compatible)
                        break;
                }
                else
                    break;
            }
        }

        if (!compatible)
            continue;

        if (score == maxScore)
            return decl;

        if (score >= matchScore) {
            matchScore = score;
            match = decl;
        }
    }

    if (match)
        return match;

    if (loc) {
        Type type = {.tag = typFunc,
                     .flags = flags,
                     .func = {.params = params,
                              .paramsCount = paramsCount,
                              .retType = makeAutoType(ctx->typeTable)}};

        logError(
            ctx->L,
            loc,
            "incompatible function reference ({u64} functions declared did "
            "not match function with signature {t})",
            (FormatArg[]){{.u64 = declarations}, {.t = &type}});

        it = decls;
        while (it) {
            logError(ctx->L,
                     &it->node->loc,
                     "found declaration with incompatible signature {t}",
                     (FormatArg[]){{.t = it->node->type}});
            it = it->next;
        }
    }
    return NULL;
}

void semanticsCheck(SemanticsContext *context, AstNode *program)
{
    program = program->metadata.node;
    context->env = makeEnvironment(context->pool, program);
    context->program = program;
    program->env = context->env;

    // clang-format off
    AstVisitor visitor = makeAstVisitor(context,
    {
        [astProgram] = checkProgram,
        [astDefine] = checkDefine,
        [astImportDecl] = checkImportDecl,
        [astPathElem] = checkPathElement,
        [astPath] = checkPath,
        [astGenericParam]= checkGenericParam,
        [astGenericDecl] = checkGenericDecl,
        [astNullLit] = checkLiterals,
        [astBoolLit] = checkLiterals,
        [astCharLit] = checkLiterals,
        [astIntegerLit] = checkLiterals,
        [astFloatLit] = checkLiterals,
        [astStringLit] = checkLiterals,
        [astStringExpr] = checkStringExpr,
        [astFuncParam] = checkFuncParam,
        [astFuncDecl] = checkFunctionDecl,
        [astVarDecl] = checkVarDecl,
        [astIdentifier] = checkIdentifier,
        [astBinaryExpr] = checkBinaryExpr,
        [astAssignExpr] = checkAssignExpr,
        [astUnaryExpr] = checkUnaryExpr,
        [astAddressOf] = checkAddressOfExpr,
        [astIndexExpr] = checkIndex,
        [astMemberExpr] = checkMember,
        [astArrayExpr] = checkArrayExpr,
        [astTupleExpr] = checkTupleExpr,
        [astStructExpr] = checkStructExpr,
        [astCallExpr] = checkCall,
        [astClosureExpr] = checkClosure,
        [ astCastExpr] = checkCastExpr,
        [ astTypedExpr] = checkTypedExpr,
        [astRangeExpr] = checkRangeExpr,
        [astTernaryExpr] = checkTernaryExpr,
        [astNewExpr] = checkNewExpr,
        [astBlockStmt] = checkBlock,
        [astReturnStmt] = checkReturnStmt,
        [astDeferStmt] = checkDeferStmt,
        [astBreakStmt] = checkBreakContinueStmt,
        [astContinueStmt] = checkBreakContinueStmt,
        [astIfStmt] = checkIfStmt,
        [astWhileStmt] = checkWhileStmt,
        [astForStmt] = checkForStmt,
        [astSwitchStmt] = checkSwitchStmt,
        [astCaseStmt] = checkCaseStmt,
        [astPrimitiveType] = checkPrimitiveType,
        [astArrayType] = checkArrayType,
        [astPointerType] = checkPointerType,
        [astTypeDecl] = checkTypeDecl,
        [astUnionDecl] = checkUnionDecl,
        [astTupleType] = checkTupleType,
        [astFuncType] = checkFuncType,
        [astVoidType] = checkBuiltinType,
        [astAutoType] = checkBuiltinType,
        [astStringType] = checkBuiltinType,
        [astOptionalType] = checkOptionalType,
        [astEnumDecl] = checkEnumDecl,
        [astStructDecl] = checkStructDecl,
        [astStructField] = checkStructField
    },

    .fallback = checkFallback,
    .dispatch = semanticsDispatch);

    // clang-format on
    AstVisitor evalVisitor = {};
    initEvalVisitor(&evalVisitor, context, program);
    context->eval.semanticsVisitor = &visitor;

    astVisit(&visitor, program);

    // environmentFree(context->eval.env);
    // environmentFreeUnusedScope(context->env);
}
