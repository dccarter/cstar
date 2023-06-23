//
// Created by Carter Mbotho on 2023-06-22.
//

#include "lang/semantics.h"
#include "lang/ttable.h"

void checkDefine(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    AstNode *option = node->define.names, *container = node->define.container;
    bool hasContainer = container != NULL;

    const Type *type = evalType(visitor, node->define.type);
    if (typeIs(type, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    Env *env = ctx->env;
    if (hasContainer) {
        if (!defineSymbol(ctx->env, ctx->L, container->ident.value, node)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }
        addModuleExport(ctx, node, container->ident.value);

        env = makeEnvironment(ctx->pool, NULL);
        pushScope(env, node);
        node->type =
            makeContainerType(ctx->typeTable, container->ident.value, env);
    }

    for (; option; option = option->next) {
        option->type = type;
        option->flags |= (node->flags & flgPublic) | flgDefine;
        cstring name = option->ident.alias ?: option->ident.value;
        if (!defineSymbol(env, ctx->L, name, option)) {
            node->type = ERROR_TYPE(ctx);
            continue;
        }

        if (!hasContainer) {
            addModuleExport(ctx, option, name);
        }
    }
}