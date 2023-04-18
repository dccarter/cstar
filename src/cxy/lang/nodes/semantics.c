/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-17
 */

#include "semantics.h"
#include "lang/ttable.h"

const Type *evalType(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    astVisit(visitor, node);
    return resolveType(ctx->typeTable, node->type);
}

u64 checkMany(AstVisitor *visitor, AstNode *node)
{
    u64 i = 0;
    for (; node; node = node->next, i++)
        astVisit(visitor, node);

    return i;
}

void addTopLevelDecl(SemanticsContext *ctx, cstring name, AstNode *node)
{
    if (name) {
        Env env = {.first = ctx->env.first, .scope = ctx->env.first};
        if (!defineSymbol(&env, ctx->L, name, node))
            return;
    }

    if (ctx->previousTopLevelDecl == ctx->currentTopLevelDecl) {
        ctx->program->program.decls = node;
        node->next = ctx->currentTopLevelDecl;
    }
    else {
        ctx->previousTopLevelDecl->next = node;
        node->next = ctx->currentTopLevelDecl;
    }
    ctx->previousTopLevelDecl = node;
}

AstNode *findSymbolByPath(SemanticsContext *ctx, const Env *env, AstNode *node)
{
    AstNode *elem = node->path.elements;
    do {
        const Type *type;
        AstNode *sym =
            findSymbol(env, ctx->L, elem->pathElement.name, &elem->loc);
        if (elem->next == NULL || sym == NULL)
            return sym;

        type = stripPointer(sym->type);
        elem = elem->next;
        switch (type->tag) {
        case typEnum:
            env = type->tEnum.env;
            break;
        case typStruct:
            env = type->tStruct.env;
            break;
        default:
            logError(ctx->L,
                     &elem->loc,
                     "type '{t}' does not support member syntax",
                     (FormatArg[]){{.t = type}});
            return NULL;
        }
    } while (true);
}

AstNode *findSymbolByNode(SemanticsContext *ctx, const Env *env, AstNode *node)
{
    switch (node->tag) {
    case astPath:
        return findSymbolByPath(ctx, env, node);
    case astIdentifier:
        return findSymbol(env, ctx->L, node->ident.value, &node->loc);
    default:
        return NULL;
    }
}