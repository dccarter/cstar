//
// Created by Carter Mbotho on 2023-08-24.
//
#include "../check.h"

#include "lang/capture.h"
#include "lang/flag.h"
#include "lang/operations.h"
#include "lang/strings.h"
#include "lang/ttable.h"
#include "lang/visitor.h"

#include "core/alloc.h"

static const Type *createStructForClosure(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *fields = NULL, *it = NULL;

    for (u64 i = 0; i < node->closureExpr.captureCount; i++) {
        Capture *capture = &node->closureExpr.capture[i];
        AstNode *field = makeAstNode(
            ctx->pool,
            &capture->node->loc,
            &(AstNode){
                .tag = astField,
                .type = capture->node->type,
                .flags = flgPrivate | capture->node->flags | flgMember,
                .structField = {
                    .name = getCapturedNodeName(capture->node),
                    .type = makeTypeReferenceNode(
                        ctx->pool, capture->node->type, &capture->node->loc)}});
        if (fields == NULL) {
            fields = it = field;
        }
        else {
            it = it->next = field;
        }
    }

    AstNode *func = makeAstNode(
        ctx->pool,
        &node->loc,
        &(AstNode){.tag = astFuncDecl,
                   .type = NULL,
                   .funcDecl = {.name = S_CallOverload,
                                .operatorOverload = opCallOverload,
                                .signature = makeFunctionSignature(
                                    ctx->pool,
                                    &(FunctionSignature){
                                        .params = node->closureExpr.params,
                                        .ret = node->closureExpr.ret}),
                                .body = node->closureExpr.body}});
    if (it)
        it->next = func;
    else
        fields = func;

    AstNode *closure =
        makeAstNode(ctx->pool,
                    &node->loc,
                    &(AstNode){.tag = astStructDecl,
                               .flags = flgClosure,
                               .structDecl = {.name = makeAnonymousVariable(
                                                  ctx->strings, "CXY__closure"),
                                              .members = fields}});
    it = fields;
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
    AstNode *fields = NULL, *it = NULL;

    for (u64 i = 0; i < node->closureExpr.captureCount; i++) {
        Capture *capture = &node->closureExpr.capture[i];
        cstring name = getCapturedNodeName(capture->node);
        AstNode *field = makeAstNode(
            ctx->pool,
            &capture->node->loc,
            &(AstNode){.tag = astFieldExpr,
                       .type = capture->node->type,
                       .flags = flgPrivate | capture->node->flags,
                       .fieldExpr = {.name = name,
                                     .value = makePath(ctx->pool,
                                                       &node->loc,
                                                       name,
                                                       capture->node->flags |
                                                           capture->flags,
                                                       capture->node->type)}});
        if (fields == NULL) {
            fields = it = field;
        }
        else {
            it = it->next = field;
        }
    }

    clearAstBody(node);
    node->tag = astStructExpr;
    node->type = type;
    node->structExpr.fields = fields;
    node->structExpr.left =
        makePath(ctx->pool, &node->loc, type->name, flgNone, type);
}

static AstNode *makeClosureForwardFunction(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *call = findMemberByName(node, S_CallOverload);
    csAssert0(call);
    AstNodeList params = {};
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
    for (AstNode *param = call->funcDecl.signature->params; param;
         param = param->next)
        insertAstNode(&params, deepCloneAstNode(ctx->pool, param));

    AstNode *forward = makeFunctionDecl(
        ctx->pool,
        &node->loc,
        makeStringConcat(ctx->strings, node->structDecl.name, "__forward"),
        params.first,
        makeTypeReferenceNode(ctx->pool, call->type->func.retType, &node->loc),
        makeExprStmt(ctx->pool,
                     &node->loc,
                     flgNone,
                     NULL,
                     NULL,
                     call->type->func.retType),
        flgNone,
        NULL,
        NULL);

    AstNodeList argList = {NULL};
    AstNode *param = forward->funcDecl.signature->params->next;
    for (; param; param = param->next) {
        insertAstNode(&argList,
                      makeResolvedPath(ctx->pool,
                                       &param->loc,
                                       param->funcParam.name,
                                       param->flags,
                                       param,
                                       NULL,
                                       param->type));
    }

    AstNode *body = forward->funcDecl.body;
    body->exprStmt.expr = makeCallExpr(
        ctx->pool,
        &node->loc,
        makeMemberExpr(
            ctx->pool,
            &node->loc,
            flgNone,
            makeCastExpr(ctx->pool,
                         &node->loc,
                         flgNone,
                         makeResolvedPath(ctx->pool,
                                          &node->loc,
                                          S_ptr,
                                          flgNone,
                                          params.first,
                                          NULL,
                                          params.first->type),
                         makeTypeReferenceNode(
                             ctx->pool,
                             makePointerType(ctx->types, node->type, flgNone),
                             &node->loc),
                         NULL,
                         makePointerType(ctx->types, node->type, flgNone)),
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
        call->type->func.retType);

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
            typeIs(node->type, Pointer)
                ? node
                : makeAddrOffExpr(
                      ctx->pool,
                      &node->loc,
                      flgNone,
                      node,
                      NULL,
                      makePointerType(ctx->types, node->type, flgNone)),
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