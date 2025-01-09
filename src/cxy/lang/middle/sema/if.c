//
// Created by Carter Mbotho on 2024-01-09.
//

#include "check.h"

void checkIfStmt(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *cond = node->ifStmt.cond, *then = node->ifStmt.body,
            *otherwise = node->ifStmt.otherwise;

    const Type *cond_ = checkType(visitor, cond);
    if (typeIs(cond_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    cond_ = unwrapType(cond_, NULL);
    if (isClassOrStructType(stripReference(cond_))) {
        if (!transformToTruthyOperator(visitor, cond)) {
            if (!typeIs(cond->type, Error))
                logError(ctx->L,
                         &cond->loc,
                         "expecting a struct that overloads the truthy `!!` in "
                         "an if statement condition, "
                         "got '{t}'",
                         (FormatArg[]){{.t = cond_}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
        cond_ = cond->type;
    }

    if (!isTruthyType(cond_)) {
        logError(
            ctx->L,
            &cond->loc,
            "expecting a truthy type in an if statement condition, got '{t}'",
            (FormatArg[]){{.t = cond_}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    bool currentReturnState = ctx->returnState;
    ctx->returnState = false;
    const Type *then_ = checkType(visitor, then);
    if (typeIs(then_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (otherwise == NULL) {
        ctx->returnState = ctx->returnState && currentReturnState;
        node->type = then_;
        return;
    }

    bool thenReturnState = ctx->returnState;
    ctx->returnState = false;
    const Type *otherwise_ = checkType(visitor, otherwise);
    ctx->returnState = thenReturnState && ctx->returnState;

    if (typeIs(otherwise_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (typeIs(then_, Auto) || typeIs(otherwise_, Auto)) {
        node->type = typeIs(then_, Auto) ? otherwise_ : then_;
        return;
    }

    if (!(typeIs(then_, Void) || typeIs(otherwise_, Void)) &&
        !isTypeAssignableFrom(then_, otherwise_)) {
        logError(ctx->L,
                 &otherwise->loc,
                 "inconsistent return type on if statement branches, then type "
                 "'{t}' is not assignable to else type '{t}'",
                 (FormatArg[]){{.t = then_}, {.t = otherwise_}});
        node->type = ERROR_TYPE(ctx);
        return;
    }
    node->type = then_;
}
