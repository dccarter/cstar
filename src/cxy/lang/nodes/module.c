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
        defineSymbol(&ctx->exports, ctx->L, name, node);
    }
}

void checkImportDecl(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *exports = node->import.exports;
    if (node->import.alias) {
        AstNode *alias = node->import.alias;
        csAssert0(nodeIs(alias, Identifier));

        defineSymbol(&ctx->env, ctx->L, alias->ident.value, exports);
    }
    else
        defineSymbol(&ctx->env, ctx->L, exports->moduleDecl.name, exports);
}

void finalizeModule(AstVisitor *visitor, AstNode *node, cstring namespace)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    if (node->program.module) {
        AstNode *module = node->program.module;
        module->type = makeModuleType(ctx->typeTable,
                                      node->program.module->moduleDecl.name);
        module->moduleDecl.env = mallocOrDie(sizeof(Env));
        *module->moduleDecl.env = ctx->exports;
        ctx->exports = (Env){NULL, NULL};
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