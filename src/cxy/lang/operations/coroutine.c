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

static AstNode *transformParamsCoroutineArgs(TypingContext *ctx, AstNode *node)
{

    AstNode *params = node->funcDecl.signature->params, *param = params;
    u64 count = countAstNodes(params);
    const Type **members = mallocOrDie(sizeof(Type *) * count);
    for (u64 i = 0; param; param = param->next, i++) {
        members[i] = param->type;
    }

    const Type *tuple = makeTupleType(ctx->types, members, count, flgNone);
    free(members);
    return makeTypeReferenceNode(
        ctx->pool, tuple, params ? manyNodesLoc(params) : &node->loc);
}

static AstNode *makeLaunchAsyncArgs(TypingContext *ctx,
                                    const AstNode *node,
                                    AstNode *obj,
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

    if (obj)
        insertAstNode(&argsList, obj);

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

static AstNode *makeCallTargetObject(TypingContext *ctx,
                                     const Type *callee,
                                     AstNode *node)
{
    AstNode *func = callee->func.decl, *parent = func->parentScope;
    if (!nodeIs(parent, ClassDecl) || hasFlag(func, Static))
        return NULL;

    if (nodeIs(node, Path)) {
        csAssert0(node->path.elements->next != NULL);
        AstNodeList elemList = {NULL};
        AstNode *elem = node->path.elements;
        while (elem->next) {
            insertAstNode(&elemList, deepCloneAstNode(ctx->pool, elem));
            elem = elem->next;
        }

        return makePathWithElements(ctx->pool,
                                    manyNodesLoc(elemList.first),
                                    elemList.last->flags,
                                    elemList.first,
                                    NULL);
    }
    else if (nodeIs(node, MemberExpr)) {
        // TOD: what if it's a function call?
        return deepCloneAstNode(ctx->pool, node->memberExpr.member);
    }
    unreachable("NOT POSSIBLE");
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
    csAssert0(coroutine);
    AstNode *parent = getParentScope(node);
    const Type *Obj =
        nodeIs(parent, Program) ? makeVoidType(ctx->types) : parent->type;

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
        makeVoidAstNode(
            ctx->pool, &node->loc, flgNone, NULL, makeVoidType(ctx->types)),
        makeBlockStmt(ctx->pool, &node->loc, NULL, NULL, NULL),
        flgPure,
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
                             makeTypeReferenceNode2(
                                 ctx->pool,
                                 Obj,
                                 &node->loc,
                                 transformParamsCoroutineArgs(ctx, node))),
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

    AstNode *callee =
        nodeIs(parent, Program)
            ? makeResolvedPath(ctx->pool,
                               &node->loc,
                               node->funcDecl.name,
                               flgNone,
                               node,
                               NULL,
                               node->type)
            : makePathWithElements(
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
                                      makeString(ctx->strings, "obj"),
                                      flgNone,
                                      makePathElement(ctx->pool,
                                                      &node->loc,
                                                      node->funcDecl.name,
                                                      flgNone,
                                                      NULL,
                                                      NULL),
                                      NULL),
                      coro->type),
                  NULL);

    AstNode *call = makeCallExpr(
        ctx->pool,
        &node->loc,
        callee,
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
        flgSyncCall,
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

    func->parentScope = parent;
    const Type *type = checkFunctionSignature(visitor, func);
    if (typeIs(type, Error))
        return node->type = ERROR_TYPE(ctx);

    func->flags |= (flgMember | flgPure | flgStatic);
    func->next = node->next;

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
    AstNode *obj = makeCallTargetObject(ctx, callee, node->callExpr.callee);
    AstNode *asyncLaunch =
        findBuiltinDecl(obj ? S_asyncLaunchMember : S_asyncLaunch);
    AstNode *Args = transformParamsCoroutineArgs(ctx, func);

    AstNode *call = makeCallExpr(
        ctx->pool,
        &node->loc,
        makeResolvedPathWithArgs(
            ctx->pool,
            &node->loc,
            S_asyncLaunch,
            flgNone,
            asyncLaunch,
            obj ? makeTypeReferenceNode2(
                      ctx->pool,
                      callee->func.retType,
                      &node->loc,
                      makeTypeReferenceNode2(
                          ctx->pool, obj->type, &node->loc, Args))
                : makeTypeReferenceNode2(
                      ctx->pool, callee->func.retType, &node->loc, Args),
            NULL),
        makeLaunchAsyncArgs(ctx,
                            func,
                            obj,
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
