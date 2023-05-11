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

#include "lang/ttable.h"

#include "core/alloc.h"

void addModuleExport(SemanticsContext *ctx, AstNode *node, cstring name)
{
    AstNode *exports = ctx->program->program.module;
    if (exports && (node->flags & flgPublic)) {
        defineSymbol(ctx->exports, ctx->L, name, node);
    }
}

void addModuleFunctionExport(SemanticsContext *ctx, AstNode *node, cstring name)
{
    AstNode *exports = ctx->program->program.module;
    if (exports && (node->flags & flgPublic)) {
        defineFunctionDecl(ctx->exports, ctx->L, name, node);
    }
}

void defineDeclarationAliasName(SemanticsContext *ctx, AstNode *node)
{
    const AstNode *alias = findAttribute(node, "alias");
    if (alias == NULL)
        return;

    if (!hasFlag(node, Native)) {
        logWarning(ctx->L,
                   &alias->loc,
                   "`@alias` attribute can only be used with a public native "
                   "declaration",
                   NULL);
        return;
    }

    const AstNode *name = findAttributeArgument(alias, "name");
    if (name == NULL || !nodeIs(name, StringLit)) {
        logWarning(
            ctx->L,
            &alias->loc,
            "`@alias` accepts a name must be provided given in format `name: "
            "\"AliasName\"",
            NULL);
        return;
    }

    defineSymbol(ctx->env, ctx->L, name->stringLiteral.value, node);
    addModuleExport(ctx, node, name->stringLiteral.value);
}

void checkImportDecl(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *exports = node->import.exports;
    if (node->import.entities) {
        AstNode *entity = node->import.entities;
        for (; entity; entity = entity->next) {
            entity->flags |= flgImportAlias;
            entity->importEntity.module = exports->moduleDecl.name;
            entity->importEntity.path =
                node->import.module->stringLiteral.value;
            defineSymbol(ctx->env, ctx->L, entity->importEntity.alias, entity);
        }
        defineSymbol(ctx->env,
                     ctx->L,
                     node->import.module->stringLiteral.value,
                     exports);
    }
    else {
        cstring name = NULL;
        if (node->import.alias) {
            AstNode *alias = node->import.alias;
            csAssert0(nodeIs(alias, Identifier));
            name = alias->ident.value;
        }
        else
            name = exports->moduleDecl.name;

        defineSymbol(ctx->env, ctx->L, name, exports);
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
        module->moduleDecl.env = mallocOrDie(sizeof(Env));
        module->moduleDecl.env = ctx->exports;
        ctx->exports = NULL;
    }
    ctx->typeTable->currentNamespace = namespace;
}

void initializeModule(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    ctx->typeTable->currentNamespace = NULL;
    if (node->program.module)
        ctx->typeTable->currentNamespace =
            node->program.module->moduleDecl.name;
}