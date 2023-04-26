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
#include "lang/ttable.h"

#include <memory.h>

static void checkProgram(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    cstring namespace = ctx->typeTable->currentNamespace;
    pushScope(&ctx->env, node);
    initializeBuiltins(ctx);

    initializeModule(visitor, node);

    if (node->program.top)
        checkMany(visitor, node->program.top);

    ctx->previousTopLevelDecl = node->program.decls;
    for (AstNode *decl = node->program.decls; decl; decl = decl->next) {
        ctx->currentTopLevelDecl = decl;
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
        findSymbol(&ctx->env, ctx->L, node->ident.value, &node->loc);
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
        findEnclosingLoopOrSwitch(&ctx->env, ctx->L, "break", &node->loc);
    else
        findEnclosingLoop(&ctx->env, ctx->L, "continue", &node->loc);

    node->type = makeVoidType(ctx->typeTable);
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
        Env env = {.first = ctx->env.first, .scope = ctx->env.first};
        if (!defineSymbol(&env, ctx->L, name, node))
            return;
    }

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
            findSymbol(env, ctx->L, elem->pathElement.name, &elem->loc);
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
        default:
            logError(ctx->L,
                     &elem->loc,
                     "type '{t}' does not support member syntax",
                     (FormatArg[]){{.t = type}});
            return NULL;
        }
    } while (true);
}

static AstNode *findSymbolOnlyByPath(const Env *env, const AstNode *node)
{
    AstNode *elem = node->path.elements;
    do {
        const Type *type;
        AstNode *sym = findSymbolOnly(
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

void transformToMemberCallExpr(AstVisitor *visitor,
                               AstNode *node,
                               AstNode *func,
                               AstNode *target,
                               cstring member,
                               AstNode *args)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *funcMember = NULL;
    if (typeIs(func->type, Generic)) {
        // infer the argument
        const Type *arg = evalType(visitor, args);
        funcMember = makeAstNode(
            ctx->pool,
            &target->loc,
            &(AstNode){
                .tag = astPathElem,
                .flags = args->flags,
                .pathElement = {.name = member,
                                .args = makeTypeReferenceNode(ctx, arg)}});
    }
    else {
        funcMember = makeAstNode(ctx->pool,
                                 &target->loc,
                                 &(AstNode){.tag = astPathElem,
                                            .flags = args->flags,
                                            .pathElement = {.name = member}});
    }

    AstNode *path = makeAstNode(ctx->pool,
                                &target->loc,
                                &(AstNode){.tag = astPath,
                                           .flags = target->flags,
                                           .type = target->type,
                                           .path = {.elements = funcMember}});

    AstNode *callee = makeAstNode(
        ctx->pool,
        &target->loc,
        &(AstNode){.tag = astMemberExpr,
                   .flags = target->flags,
                   .type = target->type,
                   .memberExpr = {.target = target, .member = path}});

    memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);
    node->tag = astCallExpr;
    node->type = NULL;
    node->callExpr.callee = callee;
    node->callExpr.args = args;
}

void semanticsCheck(AstNode *program,
                    Log *L,
                    MemPool *pool,
                    StrPool *strPool,
                    TypeTable *typeTable)
{
    SemanticsContext context = {.L = L,
                                .typeTable = typeTable,
                                .pool = pool,
                                .strPool = strPool,
                                .program = program,
                                .env = {NULL},
                                .exports = {NULL}};

    environmentInit(&context.env);
    if (program->program.module) {
        environmentInit(&context.exports);
        pushScope(&context.exports, program->program.module);
    }

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context,
    {
        [astProgram] = checkProgram,
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
        [astMacroCallExpr] = checkMacroExpr,
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
        [astStringType] = checkBuiltinType,
        [astOptionalType] = checkOptionalType,
        [astEnumDecl] = checkEnumDecl,
        [astStructDecl] = checkStructDecl,
        [astStructField] = checkStructField
    },

    .fallback = checkFallback);
    // clang-format on

    astVisit(&visitor, program);
}
