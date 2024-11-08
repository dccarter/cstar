//
// Created by Carter Mbotho on 2023-08-24.
//
#include "check.h"

#include "lang/frontend/capture.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"

#include "core/alloc.h"

static const Type *createStructForClosure(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNodeList members = {};

    for (u64 i = 0; i < node->closureExpr.captureCount; i++) {
        Capture *capture = &node->closureExpr.capture[i];
        AstNode *field = capture->field;
        capture->field = NULL;
        insertAstNode(&members, field);
    }

    AstNode *func = insertAstNode(&members,
                                  makeFunctionDecl(ctx->pool,
                                                   &node->loc,
                                                   S_CallOverload,
                                                   node->closureExpr.params,
                                                   node->closureExpr.ret,
                                                   node->closureExpr.body,
                                                   flgPublic,
                                                   NULL,
                                                   NULL));
    AstNode *init = insertAstNode(
        &members,
        makeFunctionDecl(ctx->pool,
                         &node->loc,
                         S_InitOverload,
                         NULL,
                         NULL,
                         makeBlockStmt(ctx->pool, &node->loc, NULL, NULL, NULL),
                         flgPublic,
                         NULL,
                         NULL));

    AstNode *closure = makeAstNode(
        ctx->pool,
        &node->loc,
        &(AstNode){.tag = members.first ? astClassDecl : astStructDecl,
                   .flags = flgClosure,
                   .structDecl = {
                       .name = makeAnonymousVariable(ctx->strings, "__Closure"),
                       .members = members.first}});

    AstNode *thisType =
        makePointerAstNode(ctx->pool,
                           &func->loc,
                           flgNone,
                           makeResolvedPath(ctx->pool,
                                            &func->loc,
                                            closure->structDecl.name,
                                            flgNone,
                                            closure,
                                            func->funcDecl.signature->params,
                                            NULL),
                           NULL,
                           NULL);
    func->funcDecl.this_ = makeFunctionParam(ctx->pool,
                                             &func->loc,
                                             S_this,
                                             thisType,
                                             NULL,
                                             flgNone,
                                             func->funcDecl.signature->params);
    init->funcDecl.this_ = makeFunctionParam(
        ctx->pool, &func->loc, S_this, thisType, NULL, flgNone, NULL);

    AstNode *it = members.first;
    for (; it; it = it->next) {
        it->parentScope = closure;
    }

    const Type *type = checkType(visitor, closure);
    if (typeIs(type, Error))
        return type;

    addTopLevelDeclaration(ctx, closure);
    return type;
}

static void transformClosureToStructExpr(AstVisitor *visitor,
                                         const Type *type,
                                         AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNodeList stmts = {};
    AstNode *var = insertAstNode(
        &stmts, createSmartPointerAllocClass(ctx, type, &node->loc));

    for (u64 i = 0; i < node->closureExpr.captureCount; i++) {
        Capture *capture = &node->closureExpr.capture[i];
        cstring name = getCapturedNodeName(capture->node);
        const Type *fieldType = capture->node->type;
        AstNode *value = makeResolvedPath(
            ctx->pool,
            &node->loc,
            name,
            capture->node->flags | capture->flags |
                (nodeIs(capture->node, FieldDecl) ? flgAddThis : flgNone),
            capture->node,
            NULL,
            fieldType);
        if (isClassOrStructType(fieldType) || isUnionType(fieldType) ||
            isTupleType(fieldType))
            value = makeReferenceOfExpr(
                ctx->pool,
                &value->loc,
                value->flags,
                value,
                NULL,
                makeReferenceType(ctx->types, fieldType, fieldType->flags));

        insertAstNode(
            &stmts,
            makeExprStmt(
                ctx->pool,
                &node->loc,
                flgNone,
                makeAssignExpr(
                    ctx->pool,
                    &node->loc,
                    flgNone,
                    makeMemberExpr(
                        ctx->pool,
                        &node->loc,
                        capture->flags,
                        makeResolvedIdentifier(ctx->pool,
                                               &node->loc,
                                               var->varDecl.name,
                                               0,
                                               var,
                                               NULL,
                                               var->type),
                        makeResolvedIdentifier(ctx->pool,
                                               &node->loc,
                                               name,
                                               0,
                                               findMemberDeclInType(type, name),
                                               NULL,
                                               value->type),
                        NULL,
                        value->type),
                    opAssign,
                    value,
                    NULL,
                    value->type),
                NULL,
                var->type));
    }
    insertAstNode(&stmts,
                  makeExprStmt(ctx->pool,
                               &node->loc,
                               flgNone,
                               makeResolvedPath(ctx->pool,
                                                &node->loc,
                                                var->_namedNode.name,
                                                flgMoved,
                                                var,
                                                NULL,
                                                var->type),
                               NULL,
                               var->type));

    node->tag = astStmtExpr;
    node->type = var->type;
    clearAstBody(node);
    node->stmtExpr.stmt =
        makeBlockStmt(ctx->pool, &node->loc, stmts.first, NULL, var->type);
    node->stmtExpr.stmt->flags |= flgBlockReturns;
}

static AstNode *makeClosureForwardFunction(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *call = findInAstNode(node, S_CallOverload);
    csAssert0(call);
    const Type *ret = call->type->func.retType;

    AstNodeList params = {};
    AstNodeList argList = {NULL};

    insertAstNode(
        &params,
        makeFunctionParam(
            ctx->pool,
            &node->loc,
            S_ptr,
            makeTypeReferenceNode(ctx->pool,
                                  makeVoidPointerType(ctx->types, flgNone),
                                  &node->loc),
            NULL,
            flgNone,
            NULL));

    AstNode *param = call->funcDecl.signature->params;
    for (; param; param = param->next) {
        insertAstNode(&params, shallowCloneAstNode(ctx->pool, param));
        insertAstNode(&argList,
                      makeResolvedPath(ctx->pool,
                                       &param->loc,
                                       param->funcParam.name,
                                       param->flags,
                                       params.last,
                                       NULL,
                                       param->type));
    }

    // func __Closure__fwd(self: &void, ...) : Ret =>
    //      (<&__Closure>self).op__call(...)
    AstNode *body = // (<&__Closure>self).op__call(...)
        makeExprStmt(
            ctx->pool,
            &node->loc,
            flgNone,
            // (<&__Closure>self).op__call(...)
            makeCallExpr(
                ctx->pool,
                &node->loc,
                // (<&__Closure>self).op__call
                makeMemberExpr(
                    ctx->pool,
                    &node->loc,
                    flgNone,
                    // (<&__Closure>self)
                    makeCastExpr(
                        ctx->pool,
                        &node->loc,
                        flgNone,
                        // self
                        makeResolvedPath(ctx->pool,
                                         &node->loc,
                                         S_ptr,
                                         flgNone,
                                         params.first,
                                         NULL,
                                         params.first->type),
                        // &__Closure
                        makeTypeReferenceNode(
                            ctx->pool,
                            //                            makePointerType(ctx->types,
                            //                            node->type, flgNone),
                            node->type,
                            &node->loc),
                        NULL,
                        makePointerType(ctx->types, node->type, flgNone)),
                    // op__call
                    makeResolvedPath(ctx->pool,
                                     &node->loc,
                                     S_CallOverload,
                                     call->flags,
                                     call,
                                     NULL,
                                     call->type),
                    NULL,
                    call->type),
                argList.first,
                flgNone,
                NULL,
                call->type->func.retType),
            NULL,
            NULL);
    AstNode *forward = makeFunctionDecl(
        ctx->pool,
        &node->loc,
        makeStringConcat(ctx->strings, node->structDecl.name, "__fwd"),
        // self: &void, ...
        params.first,
        // Ret
        makeTypeReferenceNode(ctx->pool, call->type->func.retType, &node->loc),
        typeIs(ret, Void)
            ? makeBlockStmt(ctx->pool, &node->loc, body, NULL, NULL)
            : body,
        flgNone,
        NULL,
        NULL);

    const Type *type = checkType(visitor, forward);
    if (typeIs(type, Error))
        return NULL;

    addTopLevelDeclaration(ctx, forward);
    node->structDecl.closureForward = forward;
    return forward;
}

AstNode *transformClosureArgument(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getConstAstVisitorContext(visitor);
    AstNode *forward =
        node->type->tStruct.decl->structDecl.closureForward
            ?: makeClosureForwardFunction(visitor, node->type->tStruct.decl);
    if (forward == NULL) {
        node->type = ERROR_TYPE(ctx);
        return node;
    }

    AstNode *next = node->next;
    AstNode *forwardRef = makeResolvedPath(ctx->pool,
                                           &node->loc,
                                           forward->funcDecl.name,
                                           flgNone,
                                           forward,
                                           NULL,
                                           forward->type);
    node->next = NULL;
    node = makeTupleExpr(
        ctx->pool,
        &node->loc,
        flgNone,
        makeCastExpr(
            ctx->pool,
            &node->loc,
            flgNone,
            //            typeIs(node->type, Pointer)
            //                ? node
            //                : makePointerOfExpr(
            //                      ctx->pool,
            //                      &node->loc,
            //                      flgNone,
            //                      node,
            //                      NULL,
            //                      makePointerType(ctx->types, node->type,
            //                      flgNone)),
            node,
            makeTypeReferenceNode(ctx->pool,
                                  makeVoidPointerType(ctx->types, flgNone),
                                  &node->loc),
            forwardRef,
            makeVoidPointerType(ctx->types, flgNone)),
        next,
        NULL);

    node->type = checkType(visitor, node);
    return node;
}

void checkClosureExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *params = node->closureExpr.params, *param = params,
            *ret = node->closureExpr.ret, *body = node->closureExpr.body;
    const Type *ret_ = ret ? checkType(visitor, ret) : makeAutoType(ctx->types);

    const Type *type = ret_;
    u64 defaultValues = 0;
    for (u64 i = 0; param; param = param->next, i++) {
        param->type ?: checkType(visitor, param);
        if (typeIs(param->type, Error)) {
            type = param->type;
            continue;
        }

        if (hasFlag(param, Variadic)) {
            logError(ctx->L,
                     &param->loc,
                     "variadic parameters are not supported on closures",
                     NULL);
            continue;
        }

        defaultValues += (param->funcParam.def != NULL);
    }

    // Bind closed in fields
    for (u64 i = 0; i < node->closureExpr.captureCount; i++) {
        Capture *capture = &node->closureExpr.capture[i];
        AstNode *field = capture->field;
        const Type *fieldType = capture->node->type;
        if (isClassOrStructType(fieldType) || isUnionType(fieldType) ||
            isTupleType(fieldType))
            fieldType = makeReferenceType(
                ctx->types, capture->node->type, capture->node->type->flags);

        field->structField.type =
            makeTypeReferenceNode(ctx->pool, fieldType, &capture->node->loc);
        field->type = fieldType;
    }

    if (typeIs(type, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    const Type *body_ = checkType(visitor, body);
    if (typeIs(body_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    type = createStructForClosure(visitor, node);
    if (typeIs(type, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    transformClosureToStructExpr(visitor, type, node);
}
