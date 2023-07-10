//
// Created by Carter Mbotho on 2023-06-22.
//

#include "lang/semantics.h"

#include "lang/flag.h"
#include "lang/ttable.h"
#include "lang/visitor.h"

void checkDefine(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    AstNode *option = node->define.names, *container = node->define.container;

    const Type *type = evalType(visitor, node->define.type);
    if (typeIs(type, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (container != NULL) {
        node->env = makeEnvironment(ctx->pool, node);
        ctx->env = environmentPush(node->env, NULL);
        for (; option; option = option->next) {
            cstring name = option->ident.alias ?: option->ident.value;
            option->flags |= (node->flags & flgPublic) | flgDefine;
            if (!defineSymbol(ctx->env, ctx->L, name, option)) {
                node->type = ERROR_TYPE(ctx);
                continue;
            }
        }
        
        ctx->env = environmentPop(ctx->env);
        if (typeIs(node->type, Error))
            return;

        if (!defineDeclaration(ctx,
                               container->ident.value,
                               NULL,
                               node,
                               hasFlag(node, Public))) //
        {
            node->type = ERROR_TYPE(ctx);
        }
    }
    else {
        for (; option; option = option->next) {
            cstring name = option->ident.alias ?: option->ident.value;
            option->flags |= (node->flags & flgPublic) | flgDefine;

            if (!defineDeclaration(
                    ctx, name, NULL, option, hasFlag(node, Public))) //
            {
                node->type = ERROR_TYPE(ctx);
            }
        }
    }
}
