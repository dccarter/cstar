//
// Created by Carter on 2023-04-27.
//

#include "lang/eval.h"
#include "lang/semantics.h"

#include "lang/ttable.h"

void checkComptime(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Env *savedUp = ctx->eval.env.up;

    environmentAttachUp(&ctx->eval.env, ctx->env);
    bool status = evaluate(ctx->eval.visitor, node);
    environmentAttachUp(&ctx->eval.env, savedUp);

    if (!status) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    // Visit the evaluated node
    if (!hasFlag(node, Visited)) {
        astVisit(visitor, node);
    }
}