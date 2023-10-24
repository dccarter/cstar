//
// Created by Carter Mbotho on 2023-10-22.
//

#include "check.h"
#include "eval.h"

#include "lang/ast.h"
#include "lang/builtins.h"
#include "lang/flag.h"
#include "lang/operations.h"
#include "lang/strings.h"
#include "lang/ttable.h"
#include "lang/types.h"
#include "lang/visitor.h"

#include "core/alloc.h"

#include <string.h>

static AstNode *transformParamsCoroutineArgs(TypingContext *ctx,
                                             AstNode *params)
{
    AstNode *param = params;
    u64 count = countAstNodes(params);
    const Type **members = mallocOrDie(sizeof(Type *) * count);
    for (u64 i = 0; param; param = param->next, i++) {
        members[i] = param->type;
    }

    const Type *tuple = makeTupleType(ctx->types, members, count, flgNone);
    return makeTypeReferenceNode(ctx->pool, tuple, manyNodesLoc(params));
}

static AstNode *makeLaunchAsyncArgs(TypingContext *ctx,
                                    const AstNode *node,
                                    AstNode *args)
{
    AstNodeList argsList = {NULL};
    AstNode *async = node->funcDecl.coroEntry;
    // {name}CoroEntry
    insertAstNode(&argsList,
                  makeResolvedPath(ctx->pool,
                                   &node->loc,
                                   async->funcDecl.name,
                                   async->flags,
                                   async,
                                   NULL,
                                   async->type));
    // "{name}"
    insertAstNode(&argsList,
                  makeStringLiteral(ctx->pool,
                                    &node->loc,
                                    node->type->name,
                                    NULL,
                                    makeStringType(ctx->types)));

    // (args)
    insertAstNode(&argsList, args);

    return argsList.first;
}

//    func helloCxyCoroutine(ptr : &void)
//    {
//        var coro = ptr : Coroutine[void, (string, i32)];
//        var promise = coro.getPromise();
//        hello(... coro.args) promise.resolve()
//    }

const Type *makeCoroutineEntry(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    cstring funcSymbol =
        makeStringConcat(ctx->strings, node->funcDecl.name, "CoroEntry");
    cstring ptrSymbol = makeString(ctx->strings, "ptr");
    cstring coroSymbol = makeString(ctx->strings, "coro");
    cstring promiseSymbol = makeString(ctx->strings, "promise");
    AstNode *coroutine = findBuiltinDecl(S_Coroutine);

    // func {name}CoroEntry(ptr: &void) {  }
    AstNode *func = makeFunctionDecl(
        ctx->pool,
        &node->loc,
        funcSymbol,
        makeFunctionParam(
            ctx->pool,
            &node->loc,
            ptrSymbol,
            makeTypeReferenceNode(ctx->pool,
                                  makeVoidPointerType(ctx->types, flgNone),
                                  &node->loc),
            NULL,
            flgNone,
            NULL),
        NULL,
        makeBlockStmt(ctx->pool, &node->loc, NULL, NULL, NULL),
        flgNone,
        NULL,
        NULL);

    AstNode *coro = makeVarDecl(
        ctx->pool,
        &node->loc,
        flgNone,
        coroSymbol,
        NULL,
        makeCastExpr(ctx->pool,
                     &node->loc,
                     flgNone,
                     makeResolvedPath(ctx->pool,
                                      &node->loc,
                                      ptrSymbol,
                                      flgNone,
                                      func->funcDecl.signature->params,
                                      NULL,
                                      func->funcDecl.signature->params->type),
                     makeResolvedPathWithArgs(
                         ctx->pool,
                         &node->loc,
                         S_Coroutine,
                         flgNone,
                         coroutine,
                         makeTypeReferenceNode2(
                             ctx->pool,
                             node->type->func.retType,
                             &node->loc,
                             transformParamsCoroutineArgs(
                                 ctx, node->funcDecl.signature->params)),
                         NULL),
                     NULL,
                     NULL),
        NULL,
        NULL);
    AstNode *body = func->funcDecl.body;
    body->blockStmt.stmts = coro;
    AstNode *promise = makeVarDecl(
        ctx->pool,
        &node->loc,
        flgNone,
        promiseSymbol,
        NULL,
        makeCallExpr(
            ctx->pool,
            &node->loc,
            makePathWithElements(
                ctx->pool,
                &node->loc,
                flgNone,
                makeResolvedPathElement(
                    ctx->pool,
                    &node->loc,
                    coroSymbol,
                    flgNone,
                    coro,
                    makePathElement(ctx->pool,
                                    &node->loc,
                                    makeString(ctx->strings, "getPromise"),
                                    flgNone,
                                    NULL,
                                    NULL),
                    NULL),
                NULL),
            NULL,
            flgNone,
            NULL,
            NULL),
        NULL,
        NULL);
    coro->next = promise;
    AstNode *call = makeCallExpr(
        ctx->pool,
        &node->loc,
        makeResolvedPath(ctx->pool,
                         &node->loc,
                         node->funcDecl.name,
                         flgNone,
                         node,
                         NULL,
                         node->type),
        makeSpreadExpr(ctx->pool,
                       &node->loc,
                       flgNone,
                       makePathWithElements(
                           ctx->pool,
                           &node->loc,
                           flgNone,
                           makeResolvedPathElement(
                               ctx->pool,
                               &node->loc,
                               coroSymbol,
                               flgNone,
                               coro,
                               makePathElement(ctx->pool,
                                               &node->loc,
                                               makeString(ctx->strings, "args"),
                                               flgNone,
                                               NULL,
                                               NULL),
                               NULL),
                           NULL),
                       NULL,
                       NULL),
        flgNone,
        NULL,
        NULL);
    AstNode *ret = NULL;
    if (typeIs(node->type->func.retType, Void)) {
        call = makeExprStmt(ctx->pool, &node->loc, flgNone, call, NULL, NULL);
    }
    else {
        call = makeVarDecl(ctx->pool,
                           &node->loc,
                           flgNone,
                           makeString(ctx->strings, "ret"),
                           NULL,
                           call,
                           NULL,
                           NULL);
        ret = makeResolvedPath(ctx->pool,
                               &node->loc,
                               call->varDecl.name,
                               flgNone,
                               call,
                               NULL,
                               NULL);
    }
    promise->next = call;
    call->next = makeCallExpr(
        ctx->pool,
        &node->loc,
        makePathWithElements(
            ctx->pool,
            &node->loc,
            flgNone,
            makeResolvedPathElement(
                ctx->pool,
                &node->loc,
                promiseSymbol,
                flgNone,
                promise,
                makePathElement(ctx->pool,
                                &node->loc,
                                makeString(ctx->strings, "resolve"),
                                flgNone,
                                NULL,
                                NULL),
                NULL),
            NULL),
        ret,
        flgNone,
        NULL,
        NULL);

    const Type *type = checkType(visitor, func);
    if (typeIs(type, Error))
        return node->type = ERROR_TYPE(ctx);

    func->flags |= (flgVisited | flgPure);
    func->next = node->next;
    func->parentScope = ctx->root.program;

    node->next = func;
    node->funcDecl.coroEntry = func;
    return node->type;
}

//
// var promise = asyncLaunch[void, (string, i32)](helloCxyCoroutine, "hello",
// ("Carter", 34:i32));
//

const Type *makeAsyncLaunchCall(AstVisitor *visitor,
                                const Type *callee,
                                AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *func = callee->func.decl;

    AstNode *asyncLaunch = findBuiltinDecl(S_asyncLaunch);
    AstNode *Args =
        transformParamsCoroutineArgs(ctx, func->funcDecl.signature->params);

    AstNode *call =
        makeCallExpr(ctx->pool,
                     &node->loc,
                     makeResolvedPathWithArgs(
                         ctx->pool,
                         &node->loc,
                         S_asyncLaunch,
                         flgNone,
                         asyncLaunch,
                         makeTypeReferenceNode2(
                             ctx->pool, callee->func.retType, &node->loc, Args),
                         NULL),
                     makeLaunchAsyncArgs(ctx,
                                         func,
                                         makeTupleExpr(ctx->pool,
                                                       &node->loc,
                                                       flgNone,
                                                       node->callExpr.args,
                                                       NULL,
                                                       Args->type)),
                     flgNone,
                     NULL,
                     NULL);

    const Type *type = checkType(visitor, call);
    if (typeIs(type, Error))
        return node->type = ERROR_TYPE(ctx);

    replaceAstNode(node, call);
    return call->type;
}
