//
// Created by Carter Mbotho on 2025-01-09.
//

#include "check.h"

#include <lang/frontend/flag.h>
#include <lang/frontend/strings.h>

static AstNode *makeCheckIsException(TypingContext *ctx, AstNode *var)
{

    const Type *type = resolveUnThisUnwrapType(var->type);
    csAssert0(typeIs(type, Union));
    type = type->tUnion.members[1].type;

    return makeBinaryExpr(
        ctx->pool,
        &var->loc,
        flgNone,
        makeResolvedPath(
            ctx->pool, &var->loc, var->_name, flgNone, var, NULL, var->type),
        opIs,
        makeTypeReferenceNode(ctx->pool, type, builtinLoc()),
        NULL,
        getPrimitiveType(ctx->types, prtBool));
}

static inline AstNode *makeStringLitOrNull(TypingContext *ctx,
                                           cstring str,
                                           AstNode *next)
{
    return str ? makeStringLiteral(ctx->pool,
                                   builtinLoc(),
                                   str,
                                   next,
                                   makeStringType(ctx->types))
               : makeNullLiteral(
                     ctx->pool, builtinLoc(), next, makeNullType(ctx->types));
}

static AstNode *makeReturnExceptionBlock(TypingContext *ctx,
                                         AstNode *var,
                                         const Type *type)
{
    AstNodeList stmts = {};
    AstNode *expr = makeCastResultTo(ctx, var, true);
    if (ctx->exceptionTrace) {
        AstNode *push = findMemberDeclInType(expr->type, S_push);
        csAssert0(push);
        expr = insertAstNode(&stmts,
                             makeVarDecl(ctx->pool,
                                         &var->loc,
                                         flgTemporary,
                                         makeString(ctx->strings, "ex"),
                                         NULL,
                                         expr,
                                         NULL,
                                         expr->type));
        AstNode *args = makeStringLitOrNull(
            ctx,
            ctx->mod,
            makeStringLitOrNull(
                ctx,
                ctx->cls,
                makeStringLitOrNull(
                    ctx,
                    ctx->fun,
                    makeStringLitOrNull(
                        ctx,
                        ctx->path,
                        makeUnsignedIntegerLiteral(
                            ctx->pool,
                            builtinLoc(),
                            var->loc.begin.row,
                            NULL,
                            getPrimitiveType(ctx->types, prtU64))))));
        insertAstNode(
            &stmts,
            makeExprStmt(
                ctx->pool,
                builtinLoc(),
                flgNone,
                makeCallExpr(ctx->pool,
                             builtinLoc(),
                             makeMemberExpr(ctx->pool,
                                            builtinLoc(),
                                            flgNone,
                                            makeResolvedPath(ctx->pool,
                                                             builtinLoc(),
                                                             expr->_name,
                                                             flgNone,
                                                             expr,
                                                             NULL,
                                                             expr->type),
                                            makeResolvedIdentifier(ctx->pool,
                                                                   builtinLoc(),
                                                                   S_push,
                                                                   0,
                                                                   push,
                                                                   NULL,
                                                                   push->type),
                                            NULL,
                                            push->type),
                             args,
                             flgNone,
                             NULL,
                             makeVoidType(ctx->types)),
                NULL,
                makeVoidType(ctx->types)));
        expr = makeResolvedPath(
            ctx->pool, &var->loc, expr->_name, flgNone, expr, NULL, expr->type);
    }
    insertAstNode(
        &stmts,
        makeReturnAstNode(ctx->pool,
                          &var->loc,
                          flgNone,
                          makeUnionValueExpr(ctx->pool,
                                             builtinLoc(),
                                             flgNone,
                                             makeUnaryExpr(ctx->pool,
                                                           &var->loc,
                                                           flgNone,
                                                           true,
                                                           opMove,
                                                           expr,
                                                           NULL,
                                                           expr->type),
                                             1,
                                             NULL,
                                             type),
                          NULL,
                          type));

    return makeBlockStmt(
        ctx->pool, &var->loc, stmts.first, NULL, makeVoidType(ctx->types));
}

static AstNode *makeAssignValueBlock(TypingContext *ctx,
                                     AstNode *var,
                                     AstNode *catchVariable)
{
    if (catchVariable == NULL)
        return NULL;

    AstNode *value = makeCastResultTo(ctx, var, false);
    return makeBlockStmt(ctx->pool,
                         &var->loc,
                         makeExprStmt(ctx->pool,
                                      &var->loc,
                                      flgNone,
                                      makeAssignExpr(ctx->pool,
                                                     &var->loc,
                                                     flgNone,
                                                     makeResolvedPathElement(
                                                         ctx->pool,
                                                         &var->loc,
                                                         catchVariable->_name,
                                                         flgNone,
                                                         catchVariable,
                                                         NULL,
                                                         catchVariable->type),
                                                     opAssign,
                                                     value,
                                                     NULL,
                                                     value->type),
                                      NULL,
                                      makeVoidType(ctx->types)),
                         NULL,
                         makeVoidType(ctx->types));
}

AstNode *makeCastResultTo(TypingContext *ctx, AstNode *var, bool except)
{

    const Type *type = resolveUnThisUnwrapType(var->type);
    type = except ? type->tUnion.members[1].type : type->tUnion.members[0].type;

    AstNode *cast =
        makeCastExpr(ctx->pool,
                     &var->loc,
                     flgBitUnionCast,
                     makeUnaryExpr(ctx->pool,
                                   &var->loc,
                                   flgNone,
                                   true,
                                   opMove,
                                   makeResolvedPath(ctx->pool,
                                                    &var->loc,
                                                    var->_name,
                                                    flgNone,
                                                    var,
                                                    NULL,
                                                    var->type),
                                   NULL,
                                   var->type),
                     makeTypeReferenceNode(ctx->pool, type, builtinLoc()),
                     NULL,
                     type);
    cast->castExpr.idx = except ? 1 : 0;
    return cast;
}

bool exceptionVerifyRaiseExpr(TypingContext *ctx,
                              const AstNode *ret,
                              AstNode *node)
{
    if (node->returnStmt.isRaise) {
        AstNode *expr = node->returnStmt.expr;
        if (ret == NULL || !isResultType(ret->type)) {
            logError(
                ctx->L,
                &node->loc,
                "function return type must be marked as explicitly raising",
                NULL);
            node->type = ERROR_TYPE(ctx);
            return false;
        }

        const Type *type =
            nodeIs(expr, CastExpr) ? expr->castExpr.expr->type : expr->type;
        bool isException = nodeIs(expr, CastExpr) ? isExceptionType(type)
                                                  : isBaseExceptionType(type);
        if (!isException) {
            logError(ctx->L,
                     &expr->loc,
                     "expecting an exception, got expression of type {t}",
                     (FormatArg[]){{.t = type}});
            node->type = ERROR_TYPE(ctx);
            return false;
        }

        if (ctx->catcher.block != NULL) {
            ctx->catcher.block->type =
                getResultTargetType(ctx->catcher.expr->type);
        }
    }
    return true;
}

void checkCatchBinaryOperator(AstVisitor *visitor,
                              AstNode *node,
                              struct ExceptionCather *catcher)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    const Type *lhsType = resolveUnThisUnwrapType(lhs->type),
               *rhsType = resolveUnThisUnwrapType(rhs->type);

    if (!isResultType(lhsType)) {
        logError(ctx->L,
                 &lhs->loc,
                 "catch used with a statement that does not throw",
                 NULL);
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (isVoidType(rhsType) && !isVoidResultType(lhsType)) {
        logError(
            ctx->L, &rhs->loc, "catch block must yield a default value", NULL);
        node->type = ERROR_TYPE(ctx);
        return;
    }

    const Type *targetType = isVoidResultType(lhsType)
                                 ? makeVoidType(ctx->types)
                                 : getResultTargetType(lhsType);
    if (!isTypeAssignableFrom(targetType, rhsType)) {
        logError(ctx->L,
                 &rhs->loc,
                 "catch default value type `{t}` not assignable to expression "
                 "value type `{t}`",
                 (FormatArg[]){{.t = rhsType}, {.t = targetType}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    // x = lhs
    AstNode *var =
        catcher->result
            ? catcher->result
            : makeVarDecl(ctx->pool,
                          &node->loc,
                          flgNone,
                          makeAnonymousVariable(ctx->strings, "_res"),
                          NULL,
                          isVoidResultType(lhsType)
                              ? lhs
                              : shallowCloneAstNode(ctx->pool, lhs),
                          NULL,
                          lhsType);
    astModifierAdd(&ctx->blockModifier, var);
    astModifierAdd(&ctx->blockModifier, catcher->variable);
    if (rhs->blockStmt.stmts != NULL) {
        if (catcher->ex) {
            // var ex = &&_res as Exception
            catcher->ex->next = rhs->blockStmt.stmts;
            rhs->blockStmt.stmts = catcher->ex;
        }
        // if (_res is Exception) { return _res as Exception; }
        astModifierAdd(
            &ctx->blockModifier,
            makeIfStmt(ctx->pool,
                       &node->loc,
                       flgNone,
                       makeCheckIsException(ctx, var),
                       rhs,
                       makeAssignValueBlock(ctx, var, catcher->variable),
                       NULL));
    }

    if (catcher->variable != NULL) {
        node->tag = astPath;
        clearAstBody(node);
        node->path.elements = makeResolvedPathElement(ctx->pool,
                                                      &node->loc,
                                                      catcher->variable->_name,
                                                      flgNone,
                                                      catcher->variable,
                                                      NULL,
                                                      catcher->variable->type);
        node->type = catcher->variable->type;
    }
    else {
        node->tag = astNoop;
        var->attrs =
            makeAttribute(ctx->pool, &node->loc, S_unused, NULL, var->attrs);
    }
}

void checkCallExceptionBubbleUp(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *type = node->type;
    if (!isResultType(type))
        return;
    const Type *ret = ctx->currentFunction->type->func.retType;
    if (!isResultType(ret)) {
        logError(ctx->L,
                 &node->loc,
                 "all exception must be handled when callees that raise are "
                 "called in a non-throwing function",
                 NULL);
        node->type = ERROR_TYPE(ctx);
        return;
    }

    // var _res = call(...)
    AstNode *res = makeVarDecl(ctx->pool,
                               &node->loc,
                               flgNone,
                               makeAnonymousVariable(ctx->strings, "_res"),
                               NULL,
                               shallowCloneAstNode(ctx->pool, node),
                               NULL,
                               node->type);
    astModifierAdd(&ctx->blockModifier, res);

    // if (_res is Exception) { return _res as Exception; }
    astModifierAdd(&ctx->blockModifier,
                   makeIfStmt(ctx->pool,
                              &node->loc,
                              flgNone,
                              makeCheckIsException(ctx, res),
                              makeReturnExceptionBlock(ctx, res, ret),
                              NULL,
                              NULL));
    if (!isVoidResultType(type)) {
        if (!nodeIs(node->parentScope, ExprStmt)) {
            node->tag = astCastExpr;
            clearAstBody(node);
            node->flags = flgBitUnionCast;
            node->type = getResultTargetType(type);
            node->castExpr.expr = makeUnaryExpr(ctx->pool,
                                                &node->loc,
                                                flgNone,
                                                true,
                                                opMove,
                                                makeResolvedPath(ctx->pool,
                                                                 &node->loc,
                                                                 res->_name,
                                                                 flgNone,
                                                                 res,
                                                                 NULL,
                                                                 res->type),
                                                NULL,
                                                res->type);
            node->castExpr.idx = 0;
            node->castExpr.to =
                makeTypeReferenceNode(ctx->pool, node->type, &node->loc);
        }
        else
            node->tag = astNoop;
    }
    else {
        node->tag = astNoop;
    }
}
