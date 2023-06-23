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

#include "lang/ttable.h"

#include <memory.h>

static void checkProgram(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    cstring namespace = ctx->typeTable->currentNamespace;
    pushScope(ctx->env, node);

    initializeBuiltins(ctx);

    initializeModule(visitor, node);

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

static void checkCastExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *expr = evalType(visitor, node->castExpr.expr);
    const Type *target = evalType(visitor, node->castExpr.to);
    if (!isTypeCastAssignable(target, expr)) {
        logError(ctx->L,
                 &node->loc,
                 "type '{t}' cannot be cast to type '{t}'",
                 (FormatArg[]){{.t = expr}, {.t = target}});
    }
    node->type = target;
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
        Env env = {.first = ctx->env->first, .scope = ctx->env->first};
        if (!defineSymbol(&env, ctx->L, name, node))
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

AstNode *findSymbolByPath(SemanticsContext *ctx,
                          const Env *env,
                          const AstNode *node)
{
    AstNode *elem = node->path.elements;
    do {
        const Type *type;
        AstNode *sym =
            elem->pathElement.resolvesTo
                ?: findSymbol(env, ctx->L, elem->pathElement.name, &elem->loc);
        if (elem->next == NULL || sym == NULL)
            return sym;

        type = stripAll(sym->type);
        elem = elem->next;
        switch (type->tag) {
        case typEnum:
            env = type->tEnum.env;
            break;
        case typThis:
            type = type->this.that;
        case typStruct:
            env = type->tStruct.env;
            break;
        case typModule:
            env = sym->moduleDecl.env;
            break;
        default:
            logError(ctx->L,
                     &elem->loc,
                     "type '{t}' does not support member syntax",
                     (FormatArg[]){{.t = type}});
            return NULL;
        }
    } while (true);
}

static SymbolRef *findSymbolRefByPath(SemanticsContext *ctx,
                                      const Env *env,
                                      const AstNode *node,
                                      bool require)
{
    AstNode *elem = node->path.elements;
    do {
        const Type *type;
        SymbolRef *ref = findSymbolRef(env,
                                       require ? ctx->L : NULL,
                                       elem->pathElement.name,
                                       require ? &elem->loc : NULL);
        if (elem->next == NULL || ref == NULL)
            return ref;

        type = stripAll(ref->node->type);
        elem = elem->next;
        switch (type->tag) {
        case typEnum:
            env = type->tEnum.env;
            break;
        case typThis:
            type = type->this.that;
        case typStruct:
            env = type->tStruct.env;
            break;
        case typModule:
            env = ref->node->moduleDecl.env;
            break;
        default:
            logError(ctx->L,
                     &elem->loc,
                     "type '{t}' does not support member syntax",
                     (FormatArg[]){{.t = type}});
            return NULL;
        }
    } while (true);
}

static SymbolRef *findSymbolRefMemberExpr(SemanticsContext *ctx,
                                          const Env *env,
                                          const AstNode *node,
                                          bool require)
{
    if (!nodeIs(node->memberExpr.member, Identifier))
        return NULL;

    AstNode *target = node->memberExpr.target;
    SymbolRef *ref = findSymbolRefByNode(ctx, env, target, require);
    if (ref == NULL || ref->node->type == NULL)
        return ref;

    const Type *type = stripAll(ref->node->type);
    switch (type->tag) {
    case typEnum:
        env = type->tEnum.env;
        break;
    case typThis:
        type = type->this.that;
    case typStruct:
        env = type->tStruct.env;
        break;
    case typModule:
        env = ref->node->moduleDecl.env;
        break;
    default:
        logError(ctx->L,
                 &target->loc,
                 "type '{t}' does not support member syntax",
                 (FormatArg[]){{.t = type}});
        return NULL;
    }

    return findSymbolRefByNode(ctx, env, node->memberExpr.member, require);
}

static AstNode *findSymbolOnlyByPath(const Env *env, const AstNode *node)
{
    AstNode *elem = node->path.elements;
    do {
        const Type *type;
        AstNode *sym =
            elem->pathElement.resolvesTo
                ?: findSymbolOnly(
                       env, elem->pathElement.alt ?: elem->pathElement.name);
        if (elem->next == NULL || sym == NULL)
            return sym;

        type = stripPointer(sym->type);
        elem = elem->next;
        switch (type->tag) {
        case typEnum:
            env = type->tEnum.env;
            break;
        case typStruct:
            env = type->tStruct.env;
            break;
        case typModule:
            env = sym->moduleDecl.env;
            break;
        default:
            return NULL;
        }
    } while (true);
}

AstNode *findSymbolByNode(SemanticsContext *ctx,
                          const Env *env,
                          const AstNode *node)
{
    switch (node->tag) {
    case astPath:
        return findSymbolByPath(ctx, env, node);
    case astIdentifier:
        return findSymbol(env, ctx->L, node->ident.value, &node->loc);
    default:
        return NULL;
    }
}

SymbolRef *findSymbolRefByNode(SemanticsContext *ctx,
                               const Env *env,
                               const AstNode *node,
                               bool require)
{
    switch (node->tag) {
    case astPath:
        return findSymbolRefByPath(ctx, env, node, require);
    case astIdentifier:
        return findSymbolRef(env,
                             require ? ctx->L : NULL,
                             node->ident.value,
                             require ? &node->loc : NULL);
    case astMemberExpr:
        return findSymbolRefMemberExpr(ctx, env, node, require);
    default:
        return NULL;
    }
}

AstNode *findSymbolOnlyByNode(const Env *env, const AstNode *node)
{
    switch (node->tag) {
    case astPath:
        return findSymbolOnlyByPath(env, node);
    case astIdentifier:
        return findSymbolOnly(env, node->ident.value);
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

void semanticsCheck(AstNode *program,
                    Log *L,
                    MemPool *pool,
                    StrPool *strPool,
                    TypeTable *typeTable,
                    Env *builtins)
{
    SemanticsContext context = {.L = L,
                                .typeTable = typeTable,
                                .pool = pool,
                                .strPool = strPool,
                                .program = program,
                                .env = NULL,
                                .exports = NULL,
                                .isBuiltins = builtins == NULL};

    context.env = makeEnvironment(pool, NULL);

    if (context.isBuiltins || program->program.module) {
        context.exports = makeEnvironment(pool, NULL);
        pushScope(context.exports, program->program.module);
    }

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context,
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
        [astCastExpr] = checkCastExpr,
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
    initEvalVisitor(&evalVisitor, &context);
    context.eval.semanticsVisitor = &visitor;

    astVisit(&visitor, program);
}
