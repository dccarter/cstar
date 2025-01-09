//
// Created by Carter Mbotho on 2025-01-09.
//

#include "check.h"

bool exceptionVerifyRaiseExpr(TypingContext *ctx,
                              const AstNode *ret,
                              AstNode *node)
{
    if (node->returnStmt.isRaise) {
        AstNode *expr = node->returnStmt.expr;
        csAssert0(nodeIs(expr, CastExpr));
        if (ret == NULL || !isResultType(ret->type)) {
            logError(
                ctx->L,
                &node->loc,
                "function return type must be marked as explicitly raising",
                NULL);
            node->type = ERROR_TYPE(ctx);
            return false;
        }

        AstNode *from = expr->castExpr.expr;
        if (!isExceptionType(from->type)) {
            logError(ctx->L,
                     &expr->loc,
                     "expecting an exception, got expression of type {t}",
                     (FormatArg[]){{.t = from->type}});
            node->type = ERROR_TYPE(ctx);
            return false;
        }
    }
    return true;
}