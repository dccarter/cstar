/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-19
 */

#include "../check.h"
#include "../codegen.h"

#include "lang/flag.h"
#include "lang/ttable.h"
#include "lang/visitor.h"

void generateTernaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->ternaryExpr.cond);
    format(ctx->state, "? ", NULL);
    astConstVisit(visitor, node->ternaryExpr.body);
    format(ctx->state, ": ", NULL);
    astConstVisit(visitor, node->ternaryExpr.otherwise);
}

void checkTernaryExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *cond = checkType(visitor, node->ternaryExpr.cond);
    const Type *body = checkType(visitor, node->ternaryExpr.body);
    const Type *otherwise = checkType(visitor, node->ternaryExpr.otherwise);

    if (typeIs(cond, Error) || //
        typeIs(body, Error) || //
        typeIs(otherwise, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (!isTruthyType(cond)) {
        logError(ctx->L,
                 &node->ternaryExpr.cond->loc,
                 "expecting a ternary expression ('?') condition type to be "
                 "truthy evaluable, "
                 "got '{t}'",
                 (FormatArg[]){{.t = cond}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (!isTypeAssignableFrom(body, otherwise)) {
        logError(ctx->L,
                 &node->loc,
                 "operands to ternary expression ('?') have different types, "
                 "'{t}' and '{t}'",
                 (FormatArg[]){{.t = body}, {.t = otherwise}});
        node->type = ERROR_TYPE(ctx);
    }
    else
        node->type = body;
}