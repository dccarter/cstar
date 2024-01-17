//
// Created by Carter Mbotho on 2023-11-03.
//

#include "bind.h"

#include "lang/middle/scope.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"


static cstring getAliasName(const AstNode *node)
{
    if (!hasFlag(node, Native))
        return NULL;
    const AstNode *alias = findAttribute(node, S_alias);

    if (alias == NULL)
        return NULL;

    const AstNode *name = findAttributeArgument(alias, S_name);

    return (nodeIs(name, StringLit)) ? name->stringLiteral.value : NULL;
}

static void defineDeclaration_(Env *env, Log *L, cstring name, AstNode *node)
{
    if (nodeIs(node, FuncDecl)) {
        defineFunctionDecl(env, L, name, node);
    }
    else
        defineForwardDeclarable(env, L, name, node);
}

void defineDeclaration(BindContext *ctx, cstring name, AstNode *node)
{
    defineDeclaration_(ctx->env, ctx->L, name, node);
    cstring alias = getAliasName(node);
    if (alias && alias != name) {
        defineDeclaration(ctx, alias, node);
    }
}

AstNode *bindAst(CompilerDriver *driver, AstNode *node)
{
    Env env;
    environmentInit(&env, node);

    node = bindAstPhase1(driver, &env, node);
    if (hasErrors(driver->L)) {
        environmentFree(&env);
        return node;
    }

    bindAstPhase2(driver, &env, node);
    environmentFree(&env);
    return node;
}
