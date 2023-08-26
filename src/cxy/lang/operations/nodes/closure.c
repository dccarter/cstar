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
        const AstNode *capture = node->closureExpr.capture[i];
        AstNode *field = makeAstNode(
            ctx->pool,
            &capture->loc,
            &(AstNode){
                .tag = astStructField,
                .type = capture->type,
                .flags = flgPrivate | capture->flags,
                .structField = {.name = getCapturedNodeName(capture),
                                .type = makeTypeReferenceNode(
                                    ctx->pool, capture->type, &capture->loc)}});
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
                                                  ctx->strings, "closure"),
                                              .members = fields}});

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
        const AstNode *capture = node->closureExpr.capture[i];
        cstring name = getCapturedNodeName(capture);
        AstNode *field = makeAstNode(
            ctx->pool,
            &capture->loc,
            &(AstNode){.tag = astFieldExpr,
                       .type = capture->type,
                       .flags = flgPrivate | capture->flags,
                       .fieldExpr = {.name = name,
                                     .value = makePath(ctx->pool,
                                                       &node->loc,
                                                       name,
                                                       capture->flags,
                                                       capture->type)}});
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