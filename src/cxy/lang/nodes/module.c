/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-24
 */

#include "lang/semantics.h"

#include "lang/flag.h"
#include "lang/ttable.h"
#include "lang/visitor.h"

#include "core/alloc.h"

bool exportDeclaration(SemanticsContext *ctx,
                       cstring name,
                       cstring alias,
                       AstNode *node)
{
    AstNode *exports = ctx->program->program.module;
    if (exports && (node->flags & flgPublic)) {
        if (nodeIs(node, FuncDecl)) {
            if (!defineFunctionDecl(ctx->exports, ctx->L, name, node))
                return false;
        }
        else {
            if (!defineSymbol(ctx->exports, ctx->L, name, node))
                return false;
        }

        if (alias) {
            return exportDeclaration(ctx, alias, NULL, node);
        }
    }
    return true;
}

bool defineDeclaration(SemanticsContext *ctx,
                       cstring name,
                       cstring alias,
                       AstNode *node,
                       bool isExported)
{
    if (nodeIs(node, FuncDecl)) {
        if (!defineFunctionDecl(ctx->env, ctx->L, name, node))
            return false;
    }
    else {
        if (!defineSymbol(ctx->env, ctx->L, name, node))
            return false;
    }

    if (alias) {
        if (!defineDeclaration(ctx, alias, NULL, node, isExported))
            return false;
    }

    if (isExported)
        return exportDeclaration(ctx, name, alias, node);

    return true;
}

void checkImportDecl(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *exports = node->import.exports, *entities = node->import.entities,
            *alias = node->import.alias, *entity = entities;

    if (entities) {
        if (alias) {
            node->env = makeEnvironment(ctx->pool, node);
            ctx->env = environmentPush(ctx->env, node->env);
        }

        for (; entity; entity = entity->next) {
            entity->flags |= flgImportAlias;
            entity->importEntity.module = exports->moduleDecl.name;
            entity->importEntity.path =
                node->import.module->stringLiteral.value;

            if (!defineSymbol(
                    ctx->env, ctx->L, entity->importEntity.alias, entity))
                node->type = ERROR_TYPE(ctx);
        }

        if (alias)
            ctx->env = environmentPop(ctx->env);

        if (!defineSymbol(ctx->env, ctx->L, alias->ident.value, node))
            node->type = ERROR_TYPE(ctx);
    }
    else {
        cstring name = NULL;
        if (alias) {
            csAssert0(nodeIs(alias, Identifier));
            name = alias->ident.value;
        }
        else
            name = exports->moduleDecl.name;

        if (!defineSymbol(ctx->env, ctx->L, name, exports)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }

        if (ctx->program->program.module)
            defineSymbol(ctx->exports, ctx->L, name, exports);
    }
}

void finalizeModule(AstVisitor *visitor, AstNode *node, cstring namespace)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    if (node->program.module) {
        AstNode *module = node->program.module;
        module->type = makeModuleType(ctx->typeTable,
                                      node->program.module->moduleDecl.name);
        ctx->exports = NULL;
    }

    ctx->typeTable->currentNamespace = namespace;
}

void initializeModule(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    ctx->typeTable->currentNamespace = NULL;
    if (node->program.module) {
        ctx->typeTable->currentNamespace =
            node->program.module->moduleDecl.name;
    }
    else if (ctx->isBuiltins) {
        node->program.module = makeAstNode(
            ctx->pool,
            builtinLoc(),
            &(AstNode){.tag = astModuleDecl, .moduleDecl = {.name = NULL}});
    }
    else
        return;

    node->program.module->env =
        makeEnvironment(ctx->pool, node->program.module);
    ctx->exports = node->program.module->env;
}