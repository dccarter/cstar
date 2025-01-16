//
// Created by Carter Mbotho on 2024-01-19.
//

#include "shake.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/visitor.h"

static AstNode *makeClosureForward(ShakeAstContext *ctx,
                                   AstNode *node,
                                   AstNode *structClosure,
                                   AstNode *op)
{
    AstNodeList params = {};
    AstNodeList args = {};
    insertAstNode(&params,
                  makeFunctionParam(ctx->pool,
                                    builtinLoc(),
                                    S_self,
                                    makeVoidPointerAstNode(
                                        ctx->pool, builtinLoc(), flgNone, NULL),
                                    NULL,
                                    flgNone,
                                    NULL));
    AstNode *param = node->closureExpr.params;
    for (; param; param = param->next) {
        insertAstNode(&params, shallowCloneAstNode(ctx->pool, param));
        insertAstNode(
            &args,
            makeUnaryExpr(ctx->pool,
                          &param->loc,
                          flgNone,
                          true,
                          opMove,
                          makeResolvedIdentifier(ctx->pool,
                                                 &param->loc,
                                                 param->funcParam.name,
                                                 param->flags,
                                                 params.last,
                                                 NULL,
                                                 NULL),
                          NULL,
                          NULL));
    }
    // var self_ = self !: Closure
    AstNode *self = makeVarDecl(
        ctx->pool,
        builtinLoc(),
        flgNone,
        makeStringConcat(ctx->strings, S_self, "_"),
        NULL,
        makeTypedExpr(
            ctx->pool,
            builtinLoc(),
            flgNone,
            // self
            makeResolvedIdentifier(
                ctx->pool, builtinLoc(), S_self, 0, params.first, NULL, NULL),
            // Closure
            makeResolvedIdentifier(ctx->pool,
                                   builtinLoc(),
                                   structClosure->structDecl.name,
                                   0,
                                   structClosure,
                                   NULL,
                                   NULL),
            NULL,
            NULL),
        NULL,
        NULL);

    // self_.op__call(args)
    AstNode *call = makeCallExpr(
        ctx->pool,
        builtinLoc(),
        // (self: &Closure).op_call
        makeMemberExpr(
            ctx->pool,
            builtinLoc(),
            flgNone,
            // (self: &Closure)
            makeResolvedIdentifier(ctx->pool,
                                   builtinLoc(),
                                   self->structDecl.name,
                                   0,
                                   structClosure,
                                   NULL,
                                   NULL),
            // op_call
            makeResolvedIdentifier(
                ctx->pool, builtinLoc(), S_CallOverload, 0, op, NULL, NULL),
            NULL,
            NULL),
        // args
        args.first,
        flgNone,
        NULL,
        NULL);
    self->next =
        makeReturnAstNode(ctx->pool, builtinLoc(), flgNone, call, NULL, NULL);

    // @static func __fwd(self: &void, ...args) => (<&Closure>
    return makeFunctionDecl(
        ctx->pool,
        builtinLoc(),
        S___fwd,
        params.first,
        node->closureExpr.ret
            ? shallowCloneAstNode(ctx->pool, node->closureExpr.ret)
            : NULL,
        makeBlockStmt(ctx->pool, builtinLoc(), self, NULL, NULL),
        flgStatic,
        NULL,
        NULL);
}

static void createStructForClosure(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    // struct __Closure { ... }
    AstNode *closure =
        makeAstNode(ctx->pool,
                    &node->loc,
                    &(AstNode){.tag = astStructDecl,
                               .flags = flgClosure,
                               .structDecl = {.name = makeAnonymousVariable(
                                                  ctx->strings, "__Closure"),
                                              .members = NULL}});

    // func op_call(...) { ... }
    AstNode *members = makeFunctionDecl(ctx->pool,
                                        &node->loc,
                                        S_CallOverload,
                                        node->closureExpr.params,
                                        node->closureExpr.ret,
                                        node->closureExpr.body,
                                        node->flags,
                                        NULL,
                                        NULL);
    members->next = makeClosureForward(ctx, node, closure, members);

    members->funcDecl.this_ = makeFunctionParam(
        ctx->pool,
        &members->loc,
        S_this,
        makePointerAstNode(ctx->pool,
                           &members->loc,
                           members->flags & flgConst,
                           makeResolvedIdentifier(ctx->pool,
                                                  &node->loc,
                                                  closure->structDecl.name,
                                                  0,
                                                  closure,
                                                  NULL,
                                                  NULL),
                           NULL,
                           NULL),
        NULL,
        flgNone,
        NULL);
    closure->structDecl.members = members;
    node->closureExpr.construct = closure;
}

void shakeClosureExpr(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    astVisitManyNodes(visitor, node->closureExpr.params);
    astVisit(visitor, node->closureExpr.ret);

    if (node->closureExpr.body && !nodeIs(node->closureExpr.body, BlockStmt)) {
        node->closureExpr.body = makeAstNode(
            ctx->pool,
            &node->closureExpr.body->loc,
            &(AstNode){
                .tag = astBlockStmt,
                .blockStmt = {.stmts = makeAstNode(
                                  ctx->pool,
                                  &node->closureExpr.body->loc,
                                  &(AstNode){.tag = astReturnStmt,
                                             .returnStmt.expr =
                                                 node->closureExpr.body})}});
    }
    astVisit(visitor, node->closureExpr.body);
    // createStructForClosure(visitor, node);
}
