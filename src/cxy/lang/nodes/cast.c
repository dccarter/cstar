/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-06-22
 */

#include "lang/semantics.h"

#include "lang/ttable.h"
#include "lang/visitor.h"

void checkCastExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *expr = evalType(visitor, node->castExpr.expr);
    const Type *target = evalType(visitor, node->castExpr.to);
    if (typeIs(expr, Error) || typeIs(target, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (!isTypeCastAssignable(target, expr)) {
        logError(ctx->L,
                 &node->loc,
                 "type '{t}' cannot be cast to type '{t}'",
                 (FormatArg[]){{.t = expr}, {.t = target}});
        node->type = ERROR_TYPE(ctx);
    }
    else
        node->type = target;
}

void checkTypedExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *expr = evalType(visitor, node->typedExpr.expr);
    const Type *type = evalType(visitor, node->typedExpr.type);

    if (typeIs(expr, Error) || typeIs(type, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (isTypeCastAssignable(type, expr) ||
        (isPointerType(expr) && isPointerType(type))) {
        node->type = type;
    }
    else {
        logError(ctx->L,
                 &node->loc,
                 "type '{t}' cannot be cast to type '{t}'",
                 (FormatArg[]){{.t = expr}, {.t = type}});
        node->type = ERROR_TYPE(ctx);
    }
}
