//
// Created by Carter on 2023-03-28.
//

#include "check.h"
#include "capture.h"
#include "scope.h"
#include "ttable.h"

#include "semantics.h"

#include "core/alloc.h"
#include "core/utils.h"

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

static void initBuiltins(SemanticsContext *ctx)
{
    {
        const Type *params[] = {getPrimitiveType(ctx->typeTable, prtChar)};
        addBuiltinFunc(
            ctx, "wputc", getPrimitiveType(ctx->typeTable, prtI32), params, 1);
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

static void checkProgram(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    pushScope(&ctx->env, node);

    initBuiltins(ctx);

    ctx->previousTopLevelDecl = node->program.decls;
    for (AstNode *decl = node->program.decls; decl; decl = decl->next) {
        ctx->currentTopLevelDecl = decl;
        astVisit(visitor, decl);
        ctx->previousTopLevelDecl = decl;
    }
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

static void checkTernaryExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *cond = evalType(visitor, node->ternaryExpr.cond);
    const Type *body = evalType(visitor, node->ternaryExpr.body);
    const Type *otherwise = evalType(visitor, node->ternaryExpr.otherwise);

    if (!isTypeAssignableFrom(getPrimitiveType(ctx->typeTable, prtBool),
                              cond)) {
        logError(ctx->L,
                 &node->ternaryExpr.cond->loc,
                 "expecting a ternary expression ('?') condition type of bool, "
                 "got '{t}'",
                 (FormatArg[]){{.t = cond}});
        node->type = ERROR_TYPE(ctx);
    }
    if (!isTypeAssignableFrom(body, otherwise)) {
        logError(ctx->L,
                 &node->loc,
                 "operands to ternary expression ('?') have different types, "
                 "'{t}' and '{t}'",
                 (FormatArg[]){{.t = body}, {.t = otherwise}});
        node->type = ERROR_TYPE(ctx);
    }
    else
        node->type = body;
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

static void checkAssign(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *left = node->assignExpr.lhs, *right = node->assignExpr.rhs;
    const Type *lhs = evalType(visitor, left);
    const Type *rhs = evalType(visitor, right);
    bool isLeftAuto = lhs == makeAutoType(ctx->typeTable);

    // TODO check r-value-ness
    if ((left->flags & flgConst) || (lhs->flags & flgConst)) {
        logError(ctx->L,
                 &node->loc,
                 "lhs of assignment expressions is a constant",
                 (FormatArg[]){{.t = lhs}});
        node->type = ERROR_TYPE(ctx);
    }
    else if (rhs->tag == typArray) {
        if (isLeftAuto)
            logError(ctx->L,
                     &node->loc,
                     "array assignment not allowed, assignment should be done "
                     "at initialisation",
                     NULL);
        else
            logError(
                ctx->L, &node->loc, "assign to an array is not allowed", NULL);
        node->type = ERROR_TYPE(ctx);
    }
    else if (!isTypeAssignableFrom(lhs, rhs)) {
        logError(ctx->L,
                 &node->assignExpr.rhs->loc,
                 "incompatible types on assigment expression, expecting '{t}', "
                 "got '{t}'",
                 (FormatArg[]){{.t = lhs}, {.t = rhs}});
        node->type = ERROR_TYPE(ctx);
    }
    if (node->type == ERROR_TYPE(ctx))
        return;

    if (isLeftAuto) {
        csAssert0(left->tag == astPath);
        const char *variable = left->path.elements->pathElement.name;
        AstNode *symbol = findSymbol(&ctx->env, ctx->L, variable, &left->loc);
        csAssert0(symbol);
        symbol->type = rhs;
        node->type = symbol->type;
    }
    else {
        node->type = lhs;
    }
}

static void checkAddressOf(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *operand = evalType(visitor, node->unaryExpr.operand);
    node->flags |= node->unaryExpr.operand->flags;
    node->type = makePointerType(
        ctx->typeTable, operand, node->unaryExpr.operand->flags);
}

static void checkGroupExpr(AstVisitor *visitor, AstNode *node)
{
    node->type = evalType(visitor, node->groupExpr.expr);
}

static void checkBlock(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *stmt = node->blockStmt.stmts;
    AstNode *prev = stmt;
    for (; stmt; stmt = stmt->next) {
        stmt->parentScope = node;
        const Type *type = evalType(visitor, stmt);
        if (stmt->tag == astReturnStmt) {
            node->type = type;
        }
        if (stmt->tag == astDeferStmt) {
            // add statement to epilogue
            insertAstNode(&node->blockStmt.epilogue, stmt->deferStmt.expr);
            stmt->deferStmt.expr->flags |= flgDeferred;
            unlinkAstNode(&node->blockStmt.stmts, prev, stmt);
        }
        prev = stmt;
    }

    if (node->type == NULL) {
        node->type = makeVoidType(ctx->typeTable);
    }
}

static void checkReturn(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *func = findEnclosingFunc(&ctx->env, ctx->L, &node->loc);
    node->type = node->returnStmt.expr
                     ? evalType(visitor, node->returnStmt.expr)
                     : makeVoidType(ctx->typeTable);
    const Type *ret = NULL;
    if (func) {
        if (func->tag == astFuncDecl && func->funcDecl.ret)
            ret = func->funcDecl.ret->type;
        else if (func->tag == astClosureExpr && func->closureExpr.ret)
            ret = func->closureExpr.ret->type;
    }

    if (ret && !isTypeAssignableFrom(ret, node->type)) {
        logError(ctx->L,
                 &node->returnStmt.expr->loc,
                 "return value of type '{t}' incompatible with function return "
                 "type '{t}",
                 (FormatArg[]){{.t = node->type}, {.t = ret}});
    }
    else if (ctx->lastReturn) {
        // we have already seen a return
        if (!isTypeAssignableFrom(ctx->lastReturn->type, node->type)) {
            logError(
                ctx->L,
                &node->returnStmt.expr->loc,
                "inconsistent return types in auto function, type "
                "'{t}' not "
                "compatible with '{t}'",
                (FormatArg[]){{.t = node->type}, {.t = ctx->lastReturn->type}});
        }
    }
    ctx->lastReturn = node;
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
    findEnclosingLoop(&ctx->env,
                      ctx->L,
                      node->tag == astBreakStmt ? "break" : "continue",
                      &node->loc);
    node->type = makeVoidType(ctx->typeTable);
}

static void checkIfStmt(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    pushScope(&ctx->env, node);

    const Type *cond = evalType(visitor, node->ifStmt.cond);
    const Type *then = evalType(visitor, node->ifStmt.body);

    if (!isTypeAssignableFrom(getPrimitiveType(ctx->typeTable, prtBool),
                              cond)) {
        logError(ctx->L,
                 &node->ternaryExpr.cond->loc,
                 "unexpected type in if statement condition, expecting "
                 "a truthy expression but got '{t}'",
                 (FormatArg[]){{.t = cond}});
        node->type = ERROR_TYPE(ctx);
    }
    else {
        node->type = then;
    }

    if (node->ifStmt.otherwise) {
        evalType(visitor, node->ifStmt.otherwise);
    }

    popScope(&ctx->env);
}

static void checkWhileStmt(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    pushScope(&ctx->env, node);
    const Type *cond = evalType(visitor, node->whileStmt.cond);
    const Type *body = evalType(visitor, node->whileStmt.body);

    if (!isTypeAssignableFrom(getPrimitiveType(ctx->typeTable, prtBool),
                              cond)) {
        logError(ctx->L,
                 &node->ternaryExpr.cond->loc,
                 "unexpected type in while statement condition, expecting "
                 "a truthy expression, but got '{t}'",
                 (FormatArg[]){{.t = cond}});
        node->type = ERROR_TYPE(ctx);
    }
    else {
        node->type = body;
    }
    popScope(&ctx->env);
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
                                .env = {NULL}};
    environmentInit(&context.env);

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context,
    {
        [astProgram] = checkProgram,
        [astPathElem] = checkPathElement,
        [astPath] = checkPath,
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
        [astAssignExpr] = checkAssign,
        [astUnaryExpr] = checkUnary,
        [astAddressOf] = checkAddressOf,
        [astIndexExpr] = checkIndex,
        [astMemberExpr] = checkMember,
        [astArrayExpr] = checkArrayExpr,
        [astTupleExpr] = checkTupleExpr,
        [astStructExpr] = checkStructExpr,
        [astGroupExpr] = checkGroupExpr,
        [astCallExpr] = checkCall,
        [astClosureExpr] = checkClosure,
        [astCastExpr] = checkCastExpr,
        [astRangeExpr] = checkRangeExpr,
        [astTernaryExpr] = checkTernaryExpr,
        [astNewExpr] = checkNewExpr,
        [astBlockStmt] = checkBlock,
        [astReturnStmt] = checkReturn,
        [astDeferStmt] = checkDeferStmt,
        [astBreakStmt] = checkBreakContinueStmt,
        [astContinueStmt] = checkBreakContinueStmt,
        [astIfStmt] = checkIfStmt,
        [astWhileStmt] = checkWhileStmt,
        [astForStmt] = checkForStmt,
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
    // clang-format off

    astVisit(&visitor, program);
}
