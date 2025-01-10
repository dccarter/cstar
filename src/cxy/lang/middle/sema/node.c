//
// Created by Carter on 2023-05-09.
//

#include "check.h"

#include <lang/middle/builtins.h>

#include <lang/frontend/flag.h>
#include <lang/frontend/strings.h>

#include <string.h>

static AstNode *makeStructInitializerForDefaults(TypingContext *ctx,
                                                 AstNode *node)
{
    AstNode *decl = typeIs(node->type, Class) ? node->type->tClass.decl
                                              : node->type->tStruct.decl;
    AstNode *member = decl ? decl->structDecl.members : NULL;
    AstNodeList init = {};
    for (; member; member = member->next) {
        if (!nodeIs(member, FieldDecl) || hasFlag(member, Static)) {
            continue;
        }
        AstNode *value =
            member->structField.value
                ? deepCloneAstNode(ctx->pool, member->structField.value)
                : makeDefaultValue(ctx->pool, member->type, &node->loc);
        insertAstNode(&init,
                      makeFieldExpr(ctx->pool,
                                    &member->loc,
                                    member->structField.name,
                                    value->flags,
                                    value,
                                    member,
                                    NULL));
    }
    return init.first;
}

static AstNode *transformRValueToLValue(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    if (node == NULL || !nodeIs(node, CallExpr))
        return node;

    cstring tmpSymbol = makeAnonymousVariable(ctx->strings, "_rv");
    AstNode *tmpVar = makeVarDecl(ctx->pool,
                                  &node->loc,
                                  (node->flags & flgConst) | flgTemporary,
                                  tmpSymbol,
                                  NULL,
                                  node,
                                  NULL,
                                  node->type);
    astVisit(visitor, node);
    astModifierAdd(&ctx->blockModifier, tmpVar);

    return makeResolvedPath(ctx->pool,
                            &node->loc,
                            tmpSymbol,
                            tmpVar->flags | flgMove,
                            tmpVar,
                            NULL,
                            tmpVar->type);
}

static AstNode *transformRValueToLValues(AstVisitor *visitor, AstNode *node)
{
    AstNodeList nodes = {};
    for (AstNode *it = node; it;) {
        AstNode *tmp = it;
        it = it->next;
        tmp->next = NULL;
        insertAstNode(&nodes, transformRValueToLValue(visitor, tmp));
    }

    return nodes.first;
}

static AstNode *createStructInitialize(TypingContext *ctx,
                                       const Type *type,
                                       const FileLoc *loc)
{
    AstNode *decl = getTypeDecl(type);
    AstNodeList init = {};
    for (AstNode *member = decl->structDecl.members; member;
         member = member->next) {
        if (nodeIs(member, FieldDecl) && member->structField.value)
            insertAstNode(
                &init,
                makeFieldExpr(
                    ctx->pool,
                    &member->loc,
                    member->structField.name,
                    member->flags,
                    deepCloneAstNode(ctx->pool, member->structField.value),
                    member,
                    NULL));
    }

    return makeVarDecl(
        ctx->pool,
        loc,
        flgReference | flgTemporary,
        makeAnonymousVariable(ctx->strings, "__obj"),
        NULL,
        makeStructExpr(ctx->pool,
                       loc,
                       flgNone,
                       makeTypeReferenceNode(ctx->pool, type, loc),
                       init.first,
                       NULL,
                       type),
        NULL,
        type);
}

static AstNode *createCallObjectInit(TypingContext *ctx,
                                     AstNode *obj,
                                     AstNode *args,
                                     const FileLoc *loc)
{
    AstNode *init = findMemberDeclInType(obj->type, S_InitOverload);
    csAssert0(init);
    return makeCallExpr(
        ctx->pool,
        loc,
        makePathWithElements(
            ctx->pool,
            loc,
            flgNone,
            makeResolvedPathElement(
                ctx->pool,
                loc,
                obj->_namedNode.name,
                flgNone,
                obj,
                makeResolvedPathElement(
                    ctx->pool, loc, S_InitOverload, flgNone, init, NULL, NULL),
                NULL),
            NULL),
        args,
        flgNone,
        NULL,
        NULL);
}

static AstNode *createCallDefaultInitializer(TypingContext *ctx,
                                             AstNode *obj,
                                             const FileLoc *loc)
{
    AstNode *init = findMemberDeclInType(obj->type, S___defaults_init);
    if (init == NULL)
        return NULL;
    return makeCallExpr(
        ctx->pool,
        loc,
        makePathWithElements(
            ctx->pool,
            loc,
            flgNone,
            makeResolvedPathElement(ctx->pool,
                                    loc,
                                    obj->_namedNode.name,
                                    flgNone,
                                    obj,
                                    makeResolvedPathElement(ctx->pool,
                                                            loc,
                                                            S___defaults_init,
                                                            flgNone,
                                                            init,
                                                            NULL,
                                                            NULL),
                                    obj->type),
            NULL),
        NULL,
        flgNone,
        NULL,
        NULL);
}

AstNode *createAllocateClass(TypingContext *ctx,
                             const Type *type,
                             const FileLoc *loc)
{
    AstNode *dctorFwd = findMemberDeclInType(type, S_DestructorFwd);
    AstNode *args = makeTypeReferenceNode(ctx->pool, type, loc);
    args->next =
        dctorFwd
            ? makeMemberExpr(ctx->pool,
                             loc,
                             flgNone,
                             makeTypeReferenceNode(ctx->pool, type, loc),
                             makeResolvedIdentifier(ctx->pool,
                                                    loc,
                                                    S_DestructorFwd,
                                                    0,
                                                    dctorFwd,
                                                    NULL,
                                                    dctorFwd->type),
                             NULL,
                             dctorFwd->type)
            : makeNullLiteral(ctx->pool, loc, NULL, makeNullType(ctx->types));

    return makeVarDecl(
        ctx->pool,
        loc,
        flgTemporary,
        makeAnonymousVariable(ctx->strings, "__obj"),
        NULL,
        makeTypedExpr(
            ctx->pool,
            loc,
            flgNone,
            makeBackendCallExpr(ctx->pool,
                                loc,
                                flgNone,
                                bfiMemAlloc,
                                args,
                                makeVoidPointerType(ctx->types, flgNone)),
            makeTypeReferenceNode(ctx->pool, type, loc),
            NULL,
            type),
        NULL,
        type);
}

void transformToMemberCallExpr(AstVisitor *visitor,
                               AstNode *node,
                               AstNode *target,
                               cstring member,
                               AstNode *args)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *callee = makeMemberExpr(
        ctx->pool,
        &target->loc,
        target->flags,
        target,
        makeIdentifier(ctx->pool, &target->loc, member, 0, NULL, NULL),
        NULL,
        NULL);
    callee->parentScope = node;
    clearAstBody(node);
    node->tag = astCallExpr;
    node->type = NULL;
    node->callExpr.callee = callee;
    node->callExpr.args = transformRValueToLValues(visitor, args);
}

const Type *transformToConstructCallExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *callee = node->callExpr.callee, *args = node->callExpr.args;
    const Type *target = resolveAndUnThisType(callee->type);

    AstNodeList stmts = {};
    AstNode *var = NULL;
    if (isClassType(target)) {
        var =
            insertAstNode(&stmts, createAllocateClass(ctx, target, &node->loc));
        insertAstNode(&stmts,
                      makeExprStmt(ctx->pool,
                                   &node->loc,
                                   flgNone,
                                   makeBackendCallExpr(
                                       ctx->pool,
                                       &node->loc,
                                       flgNone,
                                       bfiZeromem,
                                       makeResolvedPath(ctx->pool,
                                                        &node->loc,
                                                        var->_namedNode.name,
                                                        flgNone,
                                                        var,
                                                        NULL,
                                                        var->type),
                                       makeVoidType(ctx->types)),
                                   NULL,
                                   makeVoidType(ctx->types)));
        AstNode *defualtInit =
            createCallDefaultInitializer(ctx, var, &node->loc);
        if (defualtInit != NULL) {
            insertAstNode(
                &stmts,
                makeExprStmt(
                    ctx->pool, &var->loc, flgNone, defualtInit, NULL, NULL));
        }
    }
    else {
        var = insertAstNode(&stmts,
                            createStructInitialize(ctx, target, &node->loc));
    }
    insertAstNode(&stmts,
                  makeExprStmt(ctx->pool,
                               &var->loc,
                               flgNone,
                               createCallObjectInit(ctx, var, args, &node->loc),
                               NULL,
                               NULL));
    insertAstNode(&stmts,
                  makeExprStmt(ctx->pool,
                               &node->loc,
                               flgNone,
                               makeResolvedPath(ctx->pool,
                                                &node->loc,
                                                var->_namedNode.name,
                                                flgTemporary,
                                                var,
                                                NULL,
                                                NULL),
                               NULL,
                               NULL));

    node->tag = astStmtExpr;
    node->type = NULL;
    clearAstBody(node);
    node->stmtExpr.stmt =
        makeBlockStmt(ctx->pool, &node->loc, stmts.first, NULL, NULL);
    node->stmtExpr.stmt->flags |= flgBlockReturns;

    return checkType(visitor, node);
}

bool transformToTruthyOperator(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *type = node->type ?: checkType(visitor, node);
    type = stripReference(unwrapType(type, NULL));
    if (!isClassOrStructType(type))
        return false;

    const Type *member = findMemberInType(type, S_Truthy);
    if (member == NULL)
        return false;

    transformToMemberCallExpr(
        visitor,
        node,
        transformRValueToLValue(visitor, shallowCloneAstNode(ctx->pool, node)),
        S_Truthy,
        NULL);

    type = checkType(visitor, node);
    return typeIs(type, Primitive);
}

bool transformToAwaitOperator(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *operand = node->unaryExpr.operand;
    const Type *type = operand->type ?: checkType(visitor, operand);
    type = unwrapType(type, NULL);
    if (!isClassOrStructType(type))
        return false;

    const Type *member = findMemberInType(type, S_Await);
    if (member == NULL)
        return false;

    transformToMemberCallExpr(visitor, node, operand, S_Await, NULL);

    type = checkType(visitor, node);
    return !(typeIs(type, Error));
}

bool transformOptionalSome(AstVisitor *visitor, AstNode *node, AstNode *value)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *some = (AstNode *)findBuiltinDecl(S_Some);
    csAssert0(some);

    const Type *type = checkType(visitor, value);
    if (typeIs(type, Error))
        return false;

    clearAstBody(node);
    node->tag = astCallExpr;
    node->flags = flgNone;
    node->type = NULL;
    node->callExpr.callee = makeResolvedPath(
        ctx->pool, &node->loc, S_Some, flgNone, some, NULL, NULL);
    node->callExpr.args = value;

    type = checkType(visitor, node);

    return !typeIs(type, Error);
}

bool transformOptionalNone(AstVisitor *visitor, AstNode *node, const Type *type)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *none = (AstNode *)findBuiltinDecl(S_None);
    csAssert0(none);

    clearAstBody(node);
    node->tag = astCallExpr;
    node->flags = flgNone;
    node->type = NULL;

    node->callExpr.callee = makeResolvedPathWithArgs(
        ctx->pool,
        &node->loc,
        S_None,
        flgNone,
        none,
        makeTypeReferenceNode(ctx->pool, type, &node->loc),
        NULL);
    type = checkType(visitor, node);

    return !typeIs(type, Error);
}

bool transformOptionalType(AstVisitor *visitor, AstNode *node, const Type *type)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *optional = (AstNode *)findBuiltinDecl(S___Optional);
    csAssert0(optional);

    clearAstBody(node);
    node->tag = astPath;
    node->flags = flgNone;
    node->path.elements = makeResolvedPathElementWithArgs(
        ctx->pool,
        &node->loc,
        S_None,
        flgNone,
        optional,
        NULL,
        makeTypeReferenceNode(ctx->pool, type, &node->loc),
        NULL);
    type = checkType(visitor, node);

    return !typeIs(type, Error);
}

bool transformToDerefOperator(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *type = stripReference(
        node->type ?: checkType(visitor, node->unaryExpr.operand));
    if (!isClassOrStructType(type))
        return false;

    type = stripAll(type);
    if (findMemberInType(type, S_Deref) == NULL)
        return false;

    transformToMemberCallExpr(
        visitor, node, node->unaryExpr.operand, S_Deref, NULL);

    type = checkType(visitor, node);
    return !typeIs(type, Error);
}

AstNode *transformToUnionValue(TypingContext *ctx,
                               AstNode *right,
                               const Type *lhs,
                               const Type *rhs)
{
    const Type *stripped = stripPointerOnce(lhs, NULL),
               *strippedRhs = resolveAndUnThisType(stripPointerOnce(rhs, NULL));
    if (strippedRhs != lhs && typeIs(stripped, Union) &&
        stripped != strippedRhs) {
        u32 idx = findUnionTypeIndex(stripped, strippedRhs);
        csAssert0(idx != UINT32_MAX);
        return makeUnionValueExpr(
            ctx->pool, &right->loc, right->flags, right, idx, NULL, lhs);
    }
    return right;
}

AstNode *implementDefaultInitializer(AstVisitor *visitor,
                                     AstNode *node,
                                     bool isVirtual)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *member = node->classDecl.members, *last = member;
    AstNodeList initializers = {NULL};

    for (; member; member = member->next) {
        if (nodeIs(member, FieldDecl) && member->structField.value != NULL) {
            insertAstNode(
                &initializers,
                makeExprStmt(
                    ctx->pool,
                    &member->loc,
                    flgMember,
                    makeAssignExpr(ctx->pool,
                                   &member->loc,
                                   flgNone,
                                   makeResolvedPath(ctx->pool,
                                                    &member->loc,
                                                    member->structField.name,
                                                    flgAddThis,
                                                    member,
                                                    NULL,
                                                    NULL),
                                   opAssign,
                                   member->structField.value,
                                   NULL,
                                   NULL),
                    NULL,
                    NULL));
            member->structField.value = NULL;
        }
        last = member;
    }

    if (!isVirtual && initializers.first == NULL)
        return NULL;

    csAssert0(last);
    AstNode *func = makeFunctionDecl(
        ctx->pool,
        builtinLoc(),
        S___defaults_init,
        NULL,
        makeVoidAstNode(ctx->pool, builtinLoc(), flgNone, NULL, NULL),
        makeBlockStmt(ctx->pool, builtinLoc(), initializers.first, NULL, NULL),
        flgMember,
        NULL,
        NULL);
    func->funcDecl.this_ = makeFunctionParam(ctx->pool,
                                             builtinLoc(),
                                             S_this,
                                             NULL,
                                             NULL,
                                             node->flags & flgConst,
                                             NULL);
    func->parentScope = node;
    last->next = func;
    const Type *type = checkFunctionSignature(visitor, func);
    if (typeIs(type, Error)) {
        node->type = ERROR_TYPE(ctx);
        return NULL;
    }
    return func;
}
