//
// Created by Carter Mbotho on 2024-07-05.
//

#include "mem.h"

#include "lang/frontend/ast.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/types.h"
#include "lang/frontend/visitor.h"

#include "core/alloc.h"

static bool isLeftValueExpr(const AstNode *node)
{
    if (nodeIs(node, MemberExpr))
        return isLeftValueExpr(node->memberExpr.target);
    return nodeIsLeftValue(node);
}

static AstNode *addExternalDecl(MMContext *ctx, AstNode *node)
{
    NodeToExternDecl *f2e = n2eGetNodeToExternDecl(&ctx->n2e, node);
    if (f2e == NULL) {
        // new function declaration added, add extern
        AstNode *decl = makeAstNode(ctx->pool,
                                    &node->loc,
                                    &(AstNode){.tag = astExternDecl,
                                               .type = node->type,
                                               .flags = node->flags | flgPublic,
                                               .externDecl.func = node});

        n2eAddNodeToExternDecl(&ctx->n2e, node, decl);
        astModifierAdd(&ctx->root, decl);
        return decl;
    }
    else {
        return f2e->target;
    }
}

static void transformToLeftValue(MMContext *ctx, AstNode *node)
{
    AstNode *target = node->memberExpr.target;
    if (!isLeftValueExpr(target))
        return;

    AstNode *var = makeVarDecl(ctx->pool,
                               &target->loc,
                               target->flags | flgTemporary,
                               makeAnonymousVariable(ctx->strings, "__mv"),
                               NULL,
                               target,
                               NULL,
                               target->type);
    astModifierAdd(&ctx->block, var);
    node->memberExpr.target = makeResolvedIdentifier(
        ctx->pool, &target->loc, var->varDecl.name, 0, var, NULL, var->type);
}

static bool needsMemoryManagement(const AstNode *node)
{
    if (node == NULL)
        return false;
    return !hasFlags(node, flgTemporary | flgReference | flgTopLevelDecl) &&
           !hasFlag(node->type, Extern) &&
           (isClassType(node->type) || hasReferenceMembers(node->type));
}

static inline bool needsMemoryCopy(const AstNode *node)
{
    return !hasFlag(node, Move) && needsMemoryManagement(node);
}

static AstNode *makeZeromemNode(MMContext *ctx,
                                AstNode *node,
                                const Type *type,
                                AstNode *next)
{
    return makeExprStmt(
        ctx->pool,
        &node->loc,
        flgNone,
        makeBackendCallExpr(
            ctx->pool,
            &node->loc,
            flgNone,
            bfiZeromem,
            makePointerOfExpr(ctx->pool,
                              &node->loc,
                              flgNone,
                              deepCloneAstNode(ctx->pool, node),
                              NULL,
                              makePointerType(ctx->types, type, flgNone)),
            makeVoidType(ctx->types)),
        next,
        makeVoidType(ctx->types));
}

static void transformToZeromemNode(MMContext *ctx, AstNode *node, AstNode *expr)
{
    node->tag = astBackendCall;
    node->flags = flgNone, node->type = makeVoidType(ctx->types);
    node->backendCallExpr.func = bfiZeromem;
    node->backendCallExpr.args =
        makePointerOfExpr(ctx->pool,
                          &node->loc,
                          flgNone,
                          expr,
                          NULL,
                          makePointerType(ctx->types, expr->type, flgNone));
}

static AstNode *invokeTupleDestructor(MMContext *ctx, AstNode *node)
{
    const Type *type = node->type;
    const Type *destructor = type->tuple.destructorFunc;
    csAssert0(destructor);
    AstNode *dctor = addExternalDecl(ctx, destructor->func.decl);

    AstNode *args = (nodeIs(node, MemberExpr) || nodeIs(node, IndexExpr))
                        ? deepCloneAstNode(ctx->pool, node)
                        : makeResolvedIdentifier(ctx->pool,
                                                 &node->loc,
                                                 node->_namedNode.name,
                                                 0,
                                                 node,
                                                 NULL,
                                                 type);
    return makeCallExpr(
        ctx->pool,
        &node->loc,
        makeResolvedIdentifier(ctx->pool,
                               &node->loc,
                               destructor->name,
                               0,
                               dctor,
                               NULL,
                               destructor),
        makeReferenceOfExpr(ctx->pool,
                            &args->loc,
                            flgNone,
                            args,
                            NULL,
                            makeReferenceType(ctx->types, args->type, flgNone)),
        flgNone,
        NULL,
        destructor->func.retType);
}

static AstNode *invokeUnionDestructor(MMContext *ctx, AstNode *node)
{
    const Type *type = node->type;
    const Type *destructor = type->tUnion.destructorFunc;
    csAssert0(destructor);
    AstNode *dctor = addExternalDecl(ctx, destructor->func.decl);

    AstNode *args = (nodeIs(node, MemberExpr) || nodeIs(node, IndexExpr))
                        ? deepCloneAstNode(ctx->pool, node)
                        : makeResolvedIdentifier(ctx->pool,
                                                 &node->loc,
                                                 node->_namedNode.name,
                                                 0,
                                                 node,
                                                 NULL,
                                                 type);
    return makeCallExpr(
        ctx->pool,
        &node->loc,
        makeResolvedIdentifier(ctx->pool,
                               &node->loc,
                               destructor->name,
                               0,
                               dctor,
                               NULL,
                               destructor),
        makeReferenceOfExpr(ctx->pool,
                            &args->loc,
                            flgNone,
                            args,
                            NULL,
                            makeReferenceType(ctx->types, args->type, flgNone)),
        flgNone,
        NULL,
        destructor->func.retType);
}

static void transformToTupleDestruct(MMContext *ctx,
                                     AstNode *node,
                                     AstNode *expr)
{
    const Type *type = expr->type;
    const Type *destructor = type->tuple.destructorFunc;
    csAssert0(destructor);
    AstNode *dctor = addExternalDecl(ctx, destructor->func.decl);

    node->tag = astExprStmt;
    node->flags = flgNone;
    node->type = makeVoidType(ctx->types);
    clearAstBody(node);
    node->exprStmt.expr = makeCallExpr(
        ctx->pool,
        &node->loc,
        makeResolvedIdentifier(ctx->pool,
                               &node->loc,
                               destructor->name,
                               0,
                               dctor,
                               NULL,
                               destructor),
        makeReferenceOfExpr(ctx->pool,
                            &expr->loc,
                            flgNone,
                            expr,
                            NULL,
                            makeReferenceType(ctx->types, expr->type, flgNone)),
        flgNone,
        NULL,
        destructor->func.retType);
}

static void transformToUnionDestruct(MMContext *ctx,
                                     AstNode *node,
                                     AstNode *expr)
{
    const Type *type = expr->type;
    const Type *destructor = type->tUnion.destructorFunc;
    csAssert0(destructor);
    AstNode *dctor = addExternalDecl(ctx, destructor->func.decl);

    node->tag = astExprStmt;
    node->flags = flgNone;
    node->type = makeVoidType(ctx->types);
    clearAstBody(node);
    node->exprStmt.expr = makeCallExpr(
        ctx->pool,
        &node->loc,
        makeResolvedIdentifier(ctx->pool,
                               &node->loc,
                               destructor->name,
                               0,
                               dctor,
                               NULL,
                               destructor),
        makeReferenceOfExpr(ctx->pool,
                            &expr->loc,
                            flgNone,
                            expr,
                            NULL,
                            makeReferenceType(ctx->types, expr->type, flgNone)),
        flgNone,
        NULL,
        destructor->func.retType);
}

static AstNode *invokeStructDestructor(MMContext *ctx, AstNode *node)
{
    AstNode *destructor =
        findMemberDeclInType(node->type, S_DestructorOverload);
    const Type *type = destructor->type;
    csAssert0(destructor);
    AstNode *dctor = addExternalDecl(ctx, destructor);

    return makeCallExpr(
        ctx->pool,
        &node->loc,
        makeResolvedIdentifier(
            ctx->pool, &node->loc, type->name, 0, dctor, NULL, type),
        makePointerOfExpr(ctx->pool,
                          &node->loc,
                          flgNone,
                          (nodeIs(node, MemberExpr) || nodeIs(node, IndexExpr))
                              ? deepCloneAstNode(ctx->pool, node)
                              : makeResolvedIdentifier(ctx->pool,
                                                       &node->loc,
                                                       node->_namedNode.name,
                                                       0,
                                                       node,
                                                       NULL,
                                                       node->type),
                          NULL,
                          makePointerType(ctx->types, node->type, flgNone)),
        flgNone,
        NULL,
        type->func.retType);
}

static void transformToStructDestruct(MMContext *ctx,
                                      AstNode *node,
                                      AstNode *expr)
{
    AstNode *destructor =
        findMemberDeclInType(expr->type, S_DestructorOverload);
    const Type *type = destructor->type;
    csAssert0(destructor);
    AstNode *dctor = addExternalDecl(ctx, destructor);

    node->tag = astExprStmt;
    node->type = makeVoidType(ctx->types);
    node->flags = flgNone;
    clearAstBody(node);
    node->exprStmt.expr = makeCallExpr(
        ctx->pool,
        &node->loc,
        makeResolvedIdentifier(
            ctx->pool, &node->loc, type->name, 0, dctor, NULL, type),
        makePointerOfExpr(ctx->pool,
                          &node->loc,
                          flgNone,
                          expr,
                          NULL,
                          makePointerType(ctx->types, node->type, flgNone)),
        flgNone,
        NULL,
        type->func.retType);
}

static AstNode *invokeClassDropRef(MMContext *ctx, AstNode *node)
{
    AstNode *drop =
        findBuiltinDecl(ctx->traceMemory ? S_sptr_drop_trace : S_sptr_drop);
    csAssert0(drop);

    AstNode *target = (nodeIs(node, MemberExpr) || nodeIs(node, IndexExpr))
                          ? deepCloneAstNode(ctx->pool, node)
                          : makeResolvedIdentifier(ctx->pool,
                                                   &node->loc,
                                                   node->_namedNode.name,
                                                   0,
                                                   node,
                                                   NULL,
                                                   NULL);
    target->type = makeVoidPointerType(ctx->types, flgNone);
    if (ctx->traceMemory)
        target->next = makeSrLocNode(ctx->pool, &node->loc);

    return makeCallExpr(ctx->pool,
                        &node->loc,
                        makeResolvedIdentifier(ctx->pool,
                                               &node->loc,
                                               drop->_namedNode.name,
                                               0,
                                               drop,
                                               NULL,
                                               drop->type),
                        target,
                        flgNone,
                        NULL,
                        drop->type->func.retType);
}

static void transformToClassDropRef(MMContext *ctx,
                                    AstNode *node,
                                    AstNode *expr)
{
    AstNode *drop =
        findBuiltinDecl(ctx->traceMemory ? S_sptr_drop_trace : S_sptr_drop);
    csAssert0(drop);

    expr->type = makeVoidPointerType(ctx->types, flgNone);
    if (ctx->traceMemory)
        expr->next = makeSrLocNode(ctx->pool, &node->loc);

    node->tag = astExprStmt;
    node->flags = flgNone;
    node->type = makeVoidType(ctx->types);
    clearAstBody(node);
    node->exprStmt.expr =
        makeCallExpr(ctx->pool,
                     &node->loc,
                     makeResolvedIdentifier(ctx->pool,
                                            &node->loc,
                                            drop->_namedNode.name,
                                            0,
                                            drop,
                                            NULL,
                                            drop->type),
                     expr,
                     flgNone,
                     NULL,
                     drop->type->func.retType);
}

static void transformToCopyTuple(MMContext *ctx, AstNode *node)
{
    const Type *type = node->type;
    const Type *copy = type->tuple.copyFunc;
    csAssert0(copy);
    AstNode *decl = addExternalDecl(ctx, copy->func.decl);

    AstNode *args = copyAstNode(ctx->pool, node);
    node->tag = astCallExpr;
    clearAstBody(node);
    node->callExpr.callee = makeResolvedIdentifier(
        ctx->pool, &node->loc, copy->name, 0, decl, NULL, copy);
    node->callExpr.args =
        makeReferenceOfExpr(ctx->pool,
                            &args->loc,
                            flgNone,
                            args,
                            NULL,
                            makeReferenceType(ctx->types, args->type, flgNone));
}

static void transformToCopyUnion(MMContext *ctx, AstNode *node)
{
    const Type *type = node->type;
    const Type *copy = type->tUnion.copyFunc;
    csAssert0(copy);
    AstNode *decl = addExternalDecl(ctx, copy->func.decl);

    AstNode *args = copyAstNode(ctx->pool, node);
    node->tag = astCallExpr;
    clearAstBody(node);
    node->callExpr.callee = makeResolvedIdentifier(
        ctx->pool, &node->loc, copy->name, 0, decl, NULL, copy);
    node->callExpr.args =
        makeReferenceOfExpr(ctx->pool,
                            &args->loc,
                            flgNone,
                            args,
                            NULL,
                            makeReferenceType(ctx->types, args->type, flgNone));
}

static void transformToClassRefGet(MMContext *ctx, AstNode *node)
{
    AstNode *get =
        findBuiltinDecl(ctx->traceMemory ? S_sptr_get_trace : S_sptr_ref);
    csAssert0(get);

    AstNode *args = copyAstNode(ctx->pool, node);
    args->type = makeVoidPointerType(ctx->types, flgNone);
    if (ctx->traceMemory)
        args->next = makeSrLocNode(ctx->pool, &node->loc);

    node->tag = astCallExpr;
    clearAstBody(node);
    node->callExpr.callee = makeResolvedIdentifier(
        ctx->pool, &node->loc, get->_namedNode.name, 0, get, NULL, get->type);
    node->callExpr.args = args;
}

static void transformToCopyStruct(MMContext *ctx, AstNode *node)
{
    const Type *type = node->type;
    const Type *copy = findMemberInType(type, S_CopyOverload);
    csAssert0(copy);
    AstNode *decl = addExternalDecl(ctx, copy->func.decl);

    AstNode *args =
        makePointerOfExpr(ctx->pool,
                          &node->loc,
                          flgNone,
                          copyAstNode(ctx->pool, node),
                          NULL,
                          makePointerType(ctx->types, node->type, flgNone));
    node->tag = astCallExpr;
    clearAstBody(node);
    node->callExpr.callee = makeResolvedIdentifier(
        ctx->pool, &node->loc, copy->name, 0, decl, NULL, copy);
    node->callExpr.args = args;
}

void transformToMove(MMContext *ctx, AstNode *node, AstNode *expr)
{
    if (nodeIs(node, MemberExpr))
        transformToLeftValue(ctx, node);

    AstNode *body = makeVarDecl(ctx->pool,
                                &node->loc,
                                node->flags | flgTemporary,
                                makeAnonymousVariable(ctx->strings, "__tmp"),
                                NULL,
                                expr,
                                NULL,
                                node->type);
    body->next = makeZeromemNode(
        ctx,
        expr,
        isClassType(node->type) ? makeVoidPointerType(ctx->types, flgNone)
                                : node->type,
        makeExprStmt(ctx->pool,
                     &node->loc,
                     flgNone,
                     makeResolvedIdentifier(ctx->pool,
                                            &node->loc,
                                            body->varDecl.name,
                                            flgNone,
                                            body,
                                            NULL,
                                            body->type),
                     NULL,
                     body->type));

    node->tag = astStmtExpr;
    clearAstBody(node);
    node->stmtExpr.stmt =
        makeBlockStmt(ctx->pool, &node->loc, body, NULL, node->type);
    node->stmtExpr.stmt->flags |= flgBlockReturns;
}

static void transformNodeToLValue(MMContext *ctx, AstNode *node)
{
    AstNode *var = makeVarDecl(ctx->pool,
                               &node->loc,
                               node->flags | flgTemporary,
                               makeAnonymousVariable(ctx->strings, "__tmp"),
                               NULL,
                               copyAstNode(ctx->pool, node),
                               NULL,
                               node->type);
    astModifierAdd(&ctx->block, var);
    node->tag = astIdentifier;
    clearAstBody(node);
    node->ident.resolvesTo = var;
    node->ident.value = var->_namedNode.name;
}

static void addMoveSemantics(MMContext *ctx, AstNode *node)
{
    if (!isLeftValueExpr(node))
        return; // nothing to move
    AstNode *resolved = resolveAstNode(node);
    if (hasFlag(resolved, Reference))
        return; // used to mark variables as non-movable
    if (isClassType(node->type) || hasReferenceMembers(node->type))
        transformToMove(ctx, node, copyAstNode(ctx->pool, node));
}

static void transformToCopy(MMContext *ctx, AstNode *node)
{
    if (!isLeftValueExpr(node))
        return; // nothing to move

    if (isTupleType(node->type))
        transformToCopyTuple(ctx, node);
    else if (isStructType(node->type))
        transformToCopyStruct(ctx, node);
    else if (isUnionType(node->type))
        transformToCopyUnion(ctx, node);
    else
        transformToClassRefGet(ctx, node);
}

static inline AstNode *createDestruct(MMContext *ctx, AstNode *node)
{
    if (isTupleType(node->type))
        return invokeTupleDestructor(ctx, node);
    else if (isStructType(node->type))
        return invokeStructDestructor(ctx, node);
    else if (isUnionType(node->type))
        return invokeUnionDestructor(ctx, node);
    else
        return invokeClassDropRef(ctx, node);
}

static inline void transformToDestruct(MMContext *ctx,
                                       AstNode *node,
                                       AstNode *expr)
{
    if (isTupleType(expr->type))
        transformToTupleDestruct(ctx, node, expr);
    else if (isStructType(expr->type))
        transformToStructDestruct(ctx, node, expr);
    else if (isUnionType(expr->type))
        transformToUnionDestruct(ctx, node, expr);
    else
        transformToClassDropRef(ctx, node, expr);
}

static void addDestruct(MMContext *ctx, AstNode *node)
{
    AstNode *dctor = makeExprStmt(ctx->pool,
                                  &node->loc,
                                  flgNone,
                                  createDestruct(ctx, node),
                                  NULL,
                                  makeVoidType(ctx->types));
    astModifierAdd(&ctx->block, dctor);
}

static inline AstNode *createDeferDestruct(MMContext *ctx, AstNode *node)
{
    return makeDeferStmt(ctx->pool,
                         &node->loc,
                         flgVisited,
                         makeExprStmt(ctx->pool,
                                      &node->loc,
                                      flgNone,
                                      createDestruct(ctx, node),
                                      NULL,
                                      makeVoidType(ctx->types)),
                         NULL);
}

static inline void addDeferredDestruct(MMContext *ctx, AstNode *node)
{
    astModifierAddAsNext(&ctx->block, createDeferDestruct(ctx, node));
}

static inline bool exprStmtNeedsTransform(const AstNode *node)
{
    if (!needsMemoryManagement(node))
        return false;
    const AstNode *func = getCallExprCalleeFunc(node);
    if (func == NULL || hasFlag(func, Reference))
        return false;
    return true;
}

static void visitProgram(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    AstNode *decl = node->program.decls;
    astModifierInit(&ctx->root, node);

    for (; decl; decl = decl->next) {
        astModifierNext(&ctx->root, decl);
        astVisit(visitor, decl);
    }
}

static void visitBlockStmt(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    AstNode *stmt = node->blockStmt.stmts;
    astModifierInit(&ctx->block, node);

    for (; stmt; stmt = stmt->next) {
        astModifierNext(&ctx->block, stmt);
        astVisit(visitor, stmt);
    }
}

static void visitReturnStmt(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->returnStmt.expr;
    if (expr == NULL)
        return;
    ctx->localVarsInReturn = false;
    astVisit(visitor, expr);
    if (ctx->localVarsInReturn) {
        transformNodeToLValue(ctx, expr);
        ctx->localVarsInReturn = false;
        return;
    }

    if (!nodeIsLeftValue(expr) || !needsMemoryManagement(expr))
        return;

    AstNode *resolved = resolveAstNode(expr);
    if (nodeIsThisParam(resolved) &&
            hasFlag(node->returnStmt.func, Reference) ||
        hasFlag(resolved, Returned))
        return;

    transformToCopy(ctx, expr);
}

static void visitExprStmt(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->exprStmt.expr, *parent = node->parentScope;
    if (nodeIs(parent, BlockStmt) && exprStmtNeedsTransform(expr)) {
        // transform to a variable decl
        node->tag = astVarDecl;
        clearAstBody(node);
        node->type = expr->type;
        node->varDecl.type = NULL;
        node->varDecl.init = expr;
        node->varDecl.name = makeAnonymousVariable(ctx->strings, "_mm");
        astVisit(visitor, node);
    }
    else {
        astVisit(visitor, expr);
    }
}

static void visitVarDecl(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    if (needsMemoryManagement(node)) {
        if (!hasFlags(node, flgReturned | flgMoved)) {
            addDeferredDestruct(ctx, node);
            astVisit(visitor, node->varDecl.init);
            if (isLeftValueExpr(node->varDecl.init)) {
                transformToCopy(ctx, node->varDecl.init);
            }
        }
        else {
            astVisit(visitor, node->varDecl.init);
        }
    }
}

static void visitFuncDecl(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    AstNodeList dctors = {};
    AstNode *param = node->funcDecl.signature->params,
            *body = node->funcDecl.body;
    n2eAddNodeToExternDecl(&ctx->n2e, node, node);
    astVisitFallbackVisitAll(visitor, node);
    if (hasFlag(node, Extern) || node->funcDecl.body == NULL)
        return;

    if (nodeIsThisParam(param))
        param = param->next;

    for (; param; param = param->next) {
        if (needsMemoryManagement(param) && !hasFlag(param, Returned)) {
            insertAstNode(&dctors, createDeferDestruct(ctx, param));
        }
    }

    if (dctors.first) {
        dctors.last->next = body->blockStmt.stmts;
        body->blockStmt.stmts = dctors.first;
    }
}

static void visitExternDecl(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    AstNode *func = node->externDecl.func;
    if (!hasFlag(func, Extern))
        n2eAddNodeToExternDecl(&ctx->n2e, func, node);
}

static void visitAssignExpr(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    astVisitFallbackVisitAll(visitor, node);
    if (!needsMemoryManagement(node)) {
        return;
    }

    AstNode *lhs = node->assignExpr.lhs, *rhs = node->assignExpr.rhs;
    if (isLeftValueExpr(lhs))
        addDestruct(ctx, resolveAstNode(lhs));

    if (isLeftValueExpr(rhs))
        transformToCopy(ctx, rhs);
}

static void visitCallExpr(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    astVisitFallbackVisitAll(visitor, node);

    AstNode *callee = node->callExpr.callee, *arg = node->callExpr.args;
    const Type *func = callee->type;
    const AstNode *decl = getTypeDecl(func);
    if (hasFlag(decl, Extern))
        return;
    AstNode *param = nodeIs(decl, FuncDecl) ? decl->funcDecl.signature->params
                                            : decl->funcType.params;
    if (nodeIsThisParam(param)) {
        param = param->next;
        arg = arg ? arg->next : NULL;
    }

    for (; arg; arg = arg->next, param = param->next) {
        astVisit(visitor, arg);
        if (needsMemoryManagement(param) && isLeftValueExpr(arg) &&
            !hasFlag(arg, Move)) {
            transformToCopy(ctx, arg);
        }
    }
}

static void visitUnionValueExpr(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    AstNode *value = node->unionValue.value;
    astVisit(visitor, value);
    if (needsMemoryManagement(value) && isLeftValueExpr(value)) {
        transformToCopy(ctx, value);
    }
}

static void visitUnaryExpr(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    astVisitFallbackVisitAll(visitor, node);
    Operator op = node->unaryExpr.op;
    AstNode *operand = node->unaryExpr.operand;
    if (needsMemoryManagement(operand) && isLeftValueExpr(operand)) {
        if (op == opMove) {
            transformToMove(ctx, node, operand);
        }
        else if (op == opDelete) {
            AstNode *zeroMem =
                makeZeromemNode(ctx,
                                operand,
                                isClassType(operand->type)
                                    ? makeVoidPointerType(ctx->types, flgNone)
                                    : operand->type,
                                NULL);
            transformToDestruct(ctx, node, operand);
            astModifierAddAsNext(&ctx->block, zeroMem);
        }
    }
    else if (op == opDelete) {
        transformToZeromemNode(ctx, node, operand);
    }
}

static void visitTupleExpr(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    AstNode *member = node->tupleExpr.elements;
    for (; member; member = member->next) {
        astVisit(visitor, member);
        if (needsMemoryManagement(member) && isLeftValueExpr(member)) {
            transformToCopy(ctx, member);
        }
    }
}

static void visitStructExpr(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    AstNode *field = node->structExpr.fields;
    for (; field; field = field->next) {
        astVisit(visitor, field);
        AstNode *value = field->fieldExpr.value;
        if (needsMemoryManagement(value) && isLeftValueExpr(value)) {
            transformToCopy(ctx, value);
        }
    }
}

static void visitArrayExpr(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    AstNode *elem = node->arrayExpr.elements;
    for (; elem; elem = elem->next) {
        astVisit(visitor, elem);
        if (needsMemoryManagement(elem) && isLeftValueExpr(elem)) {
            transformToCopy(ctx, elem);
        }
    }
}

static void visitIdentifier(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    AstNode *resolved = resolveAstNode(node);
    if (!needsMemoryManagement(resolved) || hasFlag(resolved, Returned))
        return;
    if (nodeIs(resolved, FuncParamDecl))
        ctx->localVarsInReturn =
            ctx->localVarsInReturn || resolved->_namedNode.name != S_this;
    else
        ctx->localVarsInReturn =
            ctx->localVarsInReturn || nodeIs(resolved, Identifier);
}

void manageMemory(MMContext *context, AstNode *node)
{
    // clang-format off
    AstVisitor visitor = makeAstVisitor(context, {
        [astProgram] = visitProgram,
        [astBlockStmt] = visitBlockStmt,
        [astReturnStmt] = visitReturnStmt,
        [astExprStmt] = visitExprStmt,
        [astVarDecl] = visitVarDecl,
        [astFuncDecl] = visitFuncDecl,
        [astExternDecl] = visitExternDecl,
        [astAssignExpr] = visitAssignExpr,
        [astCallExpr] = visitCallExpr,
        [astUnionValueExpr] = visitUnionValueExpr,
        [astUnaryExpr] = visitUnaryExpr,
        [astTupleExpr] = visitTupleExpr,
        [astStructExpr] = visitStructExpr,
        [astArrayExpr] = visitArrayExpr,
        [astIdentifier] = visitIdentifier,
        [astGenericDecl] = astVisitSkip,
        [astMacroDecl] = astVisitSkip
    }, .fallback = astVisitFallbackVisitAll, .dispatch = withSavedStack);
    // clang-format on

    blockScopeContainerInit(&context->bsc, sizeof(VariableTrace));
    n2eInit(&context->n2e, context->pool);
    astVisit(&visitor, node);
    n2eDeinit(&context->n2e);
    blockScopeContainerDeinit(&context->bsc);
}
