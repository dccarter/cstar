/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-20
 */

#include "lang/codegen.h"
#include "lang/semantics.h"

#include "lang/ttable.h"

#include "core/alloc.h"

#include <memory.h>

static cstring genericDeclName(const AstNode *decl)
{
    switch (decl->tag) {
    case astFuncDecl:
        return decl->funcDecl.name;
    case astStructDecl:
        return decl->structDecl.name;
    case astTypeDecl:
        return decl->typeDecl.name;
    case astUnionDecl:
        return decl->unionDecl.name;
    default:
        unreachable("unsupported generics");
    }
}

static void setDeclName(AstNode *decl, cstring name)
{
    switch (decl->tag) {
    case astFuncDecl:
        decl->funcDecl.name = name;
        break;
    case astStructDecl:
        decl->structDecl.name = name;
        break;
    case astTypeDecl:
        decl->typeDecl.name = name;
        break;
    case astUnionDecl:
        decl->unionDecl.name = name;
        break;
    default:
        unreachable("unsupported generics");
    }
}
static AstNode *getGenericDeclFromPath(AstNode *node)
{
    while (nodeIs(node->parentScope, PathElem) ||
           nodeIs(node->parentScope, Path))
        return getGenericDeclFromPath(node->parentScope);

    return nodeIs(node->parentScope, CallExpr) ? node->parentScope : NULL;
}

AstNode *checkGenericDeclReference(AstVisitor *visitor,
                                   AstNode *generic,
                                   AstNode *node,
                                   const Env *env)
{
    SemanticsContext *ctx = getConstAstVisitorContext(visitor);
    const Type *target = generic->type;
    if (typeIs(target, Error)) {
        node->type = ERROR_TYPE(ctx);
        return NULL;
    }

    u64 count = countAstNodes(node->pathElement.args);
    if (target->generic.inferrable && count < target->generic.paramsCount) {
        AstNode *call = ctx->currentCall,
                *args = call ? call->callExpr.args : NULL;
        AstNodeList genericArgs = {NULL};
        for (u32 i = count; i < target->generic.paramsCount; i++) {
            AstNode *arg =
                getNodeAtIndex(args, target->generic.params[i].inferIndex);
            if (arg == NULL) {
                logError(ctx->L,
                         &call->loc,
                         "cannot infer generic parameter '{s}', insufficient "
                         "function "
                         "call arguments to make deduction",
                         (FormatArg[]){{.s = target->generic.params[i].name}});

                logNote(ctx->L,
                        &getNodeAtIndex(generic->genericDecl.params, i)->loc,
                        "generic argument declared here",
                        NULL);

                node->type = ERROR_TYPE(ctx);
                return NULL;
            }
            insertAstNode(&genericArgs,
                          makeTypeReferenceNode(ctx, evalType(visitor, arg)));
        }
        if (node->pathElement.args == NULL)
            node->pathElement.args = genericArgs.first;
        else
            getLastAstNode(node->pathElement.args)->next = genericArgs.first;
        count = target->generic.paramsCount;
    }

    if (count != target->generic.paramsCount) {
        logError(ctx->L,
                 &node->loc,
                 "generic parameter substitution for '{t}' error, expecting "
                 "{u64}, got {u64}",
                 (FormatArg[]){{.t = target},
                               {.u64 = target->generic.paramsCount},
                               {.u64 = count}});
        node->type = ERROR_TYPE(ctx);
        return NULL;
    }

    const Type **args = mallocOrDie(sizeof(Type *) * count);
    AstNode *param = node->pathElement.args;
    for (u64 i = 0; param; param = param->next, i++) {
        const Type *type = evalType(visitor, param);
        if (typeIs(type, Error)) {
            node->type = type;
            free(args);
            return NULL;
        }
        args[i] = type;
    }

    GetOrInset goi = makeAppliedType(
        ctx->typeTable,
        &(Type){.tag = typApplied,
                .applied = {.args = args, .argsCount = count, .from = target}});
    free(args);

    if (goi.f) {
        // substitution already found
        node->type = goi.s->applied.generated;
        node->pathElement.alt2 = node->pathElement.name;
        node->pathElement.name = node->type->name;
        return findSymbolOnly(generic->genericDecl.env, node->type->name);
    }

    // Substitute
    cstring name = makeAnonymousVariable(ctx->strPool,
                                         genericDeclName(target->generic.decl));
    AstNode *substitute = cloneAstNode(ctx->pool, target->generic.decl);
    substitute->attrs = generic->attrs;
    substitute->flags |= flgGenerated;
    setDeclName(substitute, name);
    node->pathElement.name = name;

    cstring namespace = ctx->typeTable->currentNamespace;
    ctx->typeTable->currentNamespace = target->namespace;

    substitute->next = NULL;
    Env *saveEnv = ctx->env;
    const Env *upper = getUpperEnv(generic->genericDecl.env);
    __typeof(ctx->stack) saveStack = ctx->stack;
    ctx->env = generic->genericDecl.env;
    if (upper)
        ((Env *)upper)->up = saveEnv;
    bool isMember = nodeIs(target->generic.decl, FuncDecl) &&
                    nodeIs(target->generic.decl->parentScope, StructDecl);

    pushScope(generic->genericDecl.env, NULL);
    param = node->pathElement.args;
    for (u64 i = 0; i < count; i++, param = param->next) {
        defineSymbol(generic->genericDecl.env,
                     ctx->L,
                     target->generic.params[i].name,
                     param);
    }

    addTopLevelDecl(ctx, name, substitute);
    if (isMember) {
        substitute->parentScope = target->generic.decl->parentScope;
        checkMethodDeclSignature(visitor, substitute);
        ((Type *)(goi.s))->applied.generated = substitute->type;
        checkMethodDeclBody(visitor, substitute);
        node->type = substitute->type;
    }
    else {
        node->type = evalType(visitor, substitute);
    }

    popScope(generic->genericDecl.env);
    environmentFree(&(Env){.first = ctx->env->scope->next});
    ctx->env->scope->next = NULL;
    ctx->env = saveEnv;

    if (upper)
        ((Env *)upper)->up = NULL;

    ctx->typeTable->currentNamespace = namespace;

    ((Type *)(goi.s))->applied.generated = node->type;
    Type *generated = (Type *)goi.s->applied.generated;
    if (typeIs(generated, Struct)) {
        param = node->pathElement.args;
        for (u64 i = 0; i < count; i++, param = param->next) {
            defineSymbol(generated->tStruct.env,
                         ctx->L,
                         target->generic.params[i].name,
                         param);
        }
    }
    if (env->scope->node && typeIs(env->scope->node->type, Module)) {
        defineSymbol((Env *)env, ctx->L, name, substitute);
    }

    ctx->stack = saveStack;

    return substitute;
}

void checkGenericParam(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getConstAstVisitorContext(visitor);
    // XXX: evaluate constraints

    if (defineSymbol(ctx->env, ctx->L, node->genericParam.name, node))
        node->type = makeAutoType(ctx->typeTable);
    else
        node->type = ERROR_TYPE(ctx);
}

static bool buildInferenceIndices(GenericParam *params,
                                  u64 count,
                                  AstNode *node)
{
    bool hasInference = false;
    for (u64 i = 0; i < count; i++) {
        AstNode *param = node->funcDecl.params;
        bool found = false;
        for (u32 y = 0; param; param = param->next, y++) {
            const AstNode *type = param->funcParam.type;
            if (!nodeIs(type, Path))
                continue;
            if (type->path.elements->next ||
                type->path.elements->pathElement.args)
                continue;

            if (type->path.elements->pathElement.name == params[i].name) {
                found = true;
                params[i].inferIndex = y;
                break;
            }
        }
        if (found) {
            hasInference = found;
        }
        else if (hasInference)
            return false;
    }

    return hasInference;
}

void checkGenericDecl(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getConstAstVisitorContext(visitor);
    AstNode *param = node->genericDecl.params;

    cstring name = genericDeclName(node->genericDecl.decl);
    if (!defineSymbol(ctx->env, ctx->L, name, node)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }
    addModuleExport(ctx, node, name);

    node->genericDecl.env = allocFromMemPool(ctx->pool, sizeof(Env));
    environmentInit(node->genericDecl.env);
    environmentAttachUp(node->genericDecl.env, ctx->env);
    Env *saved = ctx->env;
    ctx->env = node->genericDecl.env;

    node->genericDecl.decl->parentScope = node->parentScope;
    u64 count = countAstNodes(param);
    GenericParam *params = mallocOrDie(sizeof(GenericParam) * count);
    pushScope(ctx->env, node);
    for (u64 i = 0; param; param = param->next, i++) {
        const Type *type = evalType(visitor, param);
        if (typeIs(type, Error)) {
            node->type = type;
            break;
        }
        params[i].name = param->genericParam.name;
        params[i].decl = param;
    }

    bool isInferrable = false;
    if (nodeIs(node->genericDecl.decl, FuncDecl)) {
        isInferrable =
            buildInferenceIndices(params, count, node->genericDecl.decl);
    }

    if (node->type == NULL || !typeIs(node->type, Error)) {
        node->type =
            makeGenericType(ctx->typeTable,
                            &(Type){.tag = typGeneric,
                                    .flags = flgNone,
                                    .generic = {.params = params,
                                                .paramsCount = count,
                                                .decl = node->genericDecl.decl,
                                                .inferrable = isInferrable}});
    }

    ctx->env = saved;
    free(params);
}