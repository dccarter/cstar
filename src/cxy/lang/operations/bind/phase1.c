//
// Created by Carter Mbotho on 2023-11-03.
//

#include "bind.h"

#include "lang/capture.h"
#include "lang/flag.h"
#include "lang/scope.h"
#include "lang/strings.h"
#include "lang/visitor.h"

static void bindDefine(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    AstNode *name = node->define.names;

    astVisit(visitor, node->define.type);

    if (node->define.container) {
        defineSymbol(
            ctx->env, ctx->L, node->define.container->ident.value, node);
        pushScope(ctx->env, node);
    }

    for (; name; name = name->next) {
        name->flags |= flgDefine;
        defineSymbol(ctx->env, ctx->L, name->ident.value, name);
        if (name->ident.alias && name->ident.alias != name->ident.value)
            defineSymbol(ctx->env, ctx->L, name->ident.alias, name);
    }

    if (node->define.container)
        popScope(ctx->env);
}

static void bindImportDecl(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    const Type *exports = node->type;

    if (node->import.entities == NULL) {
        AstNode *alias = node->import.alias;
        cstring name = alias ? alias->ident.value : exports->name;
        defineSymbol(ctx->env, ctx->L, name, node);
    }
    else {
        AstNode *entity = node->import.entities;
        for (; entity; entity = entity->next) {
            defineSymbol(ctx->env,
                         ctx->L,
                         entity->importEntity.alias
                             ?: entity->importEntity.name,
                         entity->importEntity.target);
        }
    }
}

void bindDeclaration(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    defineDeclaration(ctx, getDeclarationName(node), node);
}

AstNode *bindAstPhase1(CompilerDriver *driver, Env *env, AstNode *node)
{
    BindContext context = {.env = env, .L = driver->L, .pool = &driver->pool};

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context, {
        [astDefine] = bindDefine,
        [astImportDecl] = bindImportDecl,
        [astGenericDecl] = bindDeclaration,
        [astFuncDecl] = bindDeclaration,
        [astMacroDecl] = bindDeclaration,
        [astTypeDecl] = bindDeclaration,
        [astUnionDecl] = bindDeclaration,
        [astEnumDecl] = bindDeclaration,
        [astStructDecl] = bindDeclaration,
        [astClassDecl] = bindDeclaration,
        [astInterfaceDecl] = bindDeclaration,
    }, .fallback = astVisitFallbackVisitAll);
    // clang-format on

    astVisit(&visitor, node);

    return node;
}
