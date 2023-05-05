/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-18
 */

#include "lang/codegen.h"
#include "lang/semantics.h"

#include "lang/ttable.h"

#include "core/alloc.h"

#include <memory.h>

static inline const Type *makeFunctionDeclType(SemanticsContext *ctx,
                                               AstNode *node,
                                               const Type *ret,
                                               const Type **params,
                                               u64 paramsCount,
                                               bool hasDefaultValues)
{
    return makeFuncType(
        ctx->typeTable,
        &(Type){.tag = typFunc,
                .name = node->funcDecl.name,
                .flags = node->flags,
                .func = {.retType = ret,
                         .params = params,
                         .paramsCount = paramsCount,
                         .decl = node,
                         .defaultValuesCount = hasDefaultValues}});
}

static inline bool isInlineFunction(const AstNode *node)
{
    return findAttribute(node, "inline") != NULL;
}

static const Type *transformFuncTypeParam(SemanticsContext *ctx,
                                          const Type *type)
{
    // change (i32) => X to (&void, i32) => X
    const Type **newParams =
        mallocOrDie(sizeof(Type *) * type->func.paramsCount + 1);
    newParams[0] = makeVoidPointerType(ctx->typeTable, flgNone);
    for (u64 i = 0; i < type->func.paramsCount; i++)
        newParams[i + 1] = type->func.params[i];

    type =
        makeFuncType(ctx->typeTable,
                     &(Type){.tag = typFunc,
                             .flags = type->flags | flgFuncTypeParam,
                             .func = {.params = newParams,
                                      .paramsCount = type->func.paramsCount + 1,
                                      .retType = type->func.retType}});
    type = makeTupleType(
        ctx->typeTable,
        (const Type *[]){makeVoidPointerType(ctx->typeTable, flgNone), type},
        2,
        type->flags | flgFuncTypeParam);

    free(newParams);

    return type;
}

static const Type **checkFunctionParams(AstVisitor *visitor,
                                        AstNode *node,
                                        u64 *paramsCount,
                                        bool *withDefaultValues)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type **params = NULL;

    AstNode *param = node->funcDecl.params;
    bool isVariadic = false;

    *paramsCount = countAstNodes(node->funcDecl.params);
    if (*paramsCount == 0) {
        return NULL;
    }

    params = mallocOrDie(sizeof(Type *) * *paramsCount);

    for (u64 i = 0; param; param = param->next, i++) {
        param->parentScope = node;
        params[i] = evalType(visitor, param);
        if (isVariadic && (param->flags & flgVariadic)) {
            logError(ctx->L,
                     &param->loc,
                     "variadic parameters should the last parameter type in "
                     "function declaration",
                     NULL);
            continue;
        }

        isVariadic = (param->flags & flgVariadic);

        if (*withDefaultValues && param->funcParam.def == NULL) {
            logError(ctx->L,
                     &param->loc,
                     "parameter cannot be declared without a default value",
                     NULL);
            continue;
        }
        *withDefaultValues = (param->funcParam.def != NULL);
        if (params[i]->tag == typFunc) {
            params[i] = transformFuncTypeParam(ctx, params[i]);
            param->type = params[i];

            param->flags |= flgFuncTypeParam;
            node->flags |= flgClosureStyle;
        }
    }

    return params;
}

static bool validateOperatorOverloadFunc(SemanticsContext *ctx, AstNode *node)
{
    u64 count = countAstNodes(node->funcDecl.params);
    Operator op = node->funcDecl.operatorOverload;
    FileLoc loc = fileposSubrange(&node->loc, &node->funcDecl.body->loc);
    switch (op) {
#define f(OP, ...) case op##OP:
        AST_BINARY_EXPR_LIST(f)
        if (count != 1) {
            logError(
                ctx->L,
                &loc,
                "unexpected binary operator `{s}` overload parameter count, "
                "expecting '1', got '{u64}'",
                (FormatArg[]){{.s = getBinaryOpString(op)}, {.u64 = count}});
            return false;
        }
        break;
#undef f

    case opIndexOverload:
        if (count != 1) {
            logError(ctx->L,
                     &loc,
                     "unexpected index operator `[]` overload parameter count, "
                     "expecting '1', got '{u64}'",
                     (FormatArg[]){{.u64 = count}});
            return false;
        }
        break;

    case opIndexAssignOverload:
        if (count != 2) {
            logError(ctx->L,
                     &loc,
                     "unexpected index assign operator `[]=` overload "
                     "parameter count, expecting '2', got '{u64}'",
                     (FormatArg[]){{.u64 = count}});
            return false;
        }
        break;

    case opDelete:
        if (count != 0) {
            logError(ctx->L,
                     &loc,
                     "unexpected delete operator `delete` overload "
                     "parameter count, expecting '0', got '{u64}'",
                     (FormatArg[]){{.u64 = count}});
            return false;
        }
        break;

    case opStringOverload:
        if (count != 0) {
            logError(ctx->L,
                     &loc,
                     "unexpected string operator `str` overload "
                     "parameter count, expecting '0', got '{u64}'",
                     (FormatArg[]){{.u64 = count}});
            return false;
        }
        break;
    default:
        break;
    }

    return true;
}

void generateFuncParam(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    generateTypeUsage(ctx, node->type);
    format(ctx->state, " {s}", (FormatArg[]){{.s = node->funcParam.name}});
}

void generateFunctionDefinition(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    TypeTable *table = (ctx)->types;

    const AstNode *parent = node->parentScope;
    bool isMember = parent && parent->tag == astStructDecl;

    if (hasFlag(node, Native)) {
        // Generated on prologue statement
        if (ctx->namespace) {
            format(ctx->state, "#define ", NULL);
            writeNamespace(ctx, NULL);
            format(ctx->state,
                   "{s} {s}",
                   (FormatArg[]){{.s = node->funcDecl.name},
                                 {.s = node->funcDecl.name}});
        }
        const char *name = getNativeDeclarationAliasName(node);
        if (name) {
            format(ctx->state, "\n#define ", NULL);
            writeNamespace(ctx, NULL);
            format(ctx->state,
                   "{s} {s}",
                   (FormatArg[]){{.s = name}, {.s = node->funcDecl.name}});
        }

        return;
    }

    if (hasFlag(node, Closure)) {
        generateClosureExpr(visitor, node);
        return;
    }
    cstring namespace = ctx->namespace;
    if (hasFlag(node, Generated))
        ctx->namespace = node->type->namespace;

    if (!isMember && hasFlag(node, Main)) {
        format(ctx->state, "typedef ", NULL);
        writeTypename(ctx, node->type->func.params[0]);
        format(ctx->state, " cxy_main_args_t;\n", NULL);
        if (isIntegerType(node->type->func.retType)) {
            format(ctx->state,
                   "#define CXY_MAIN_INVOKE(...) return "
                   "cxy_main(__VA_ARGS__)\n\n",
                   NULL);
        }
        else {
            format(ctx->state,
                   "#define CXY_MAIN_INVOKE(...) cxy_main(__VA_ARGS__); "
                   "return EXIT_SUCCESS\n\n",
                   NULL);
        }
    }

    if (isInlineFunction(node))
        format(ctx->state, "attr(always_inline)\n", NULL);

    generateTypeUsage(ctx, node->type->func.retType);
    if (typeIs(node->type->func.retType, This))
        format(ctx->state, "*", NULL);

    if (isMember) {
        format(ctx->state, " ", NULL);
        //        if (hasFlag(node, Generated))
        //            writeDeclNamespace(ctx, node->type->namespace, NULL);
        //        else
        //            writeNamespace(ctx, NULL);
        writeTypename(ctx, parent->type);
        format(ctx->state, "__{s}", (FormatArg[]){{.s = node->funcDecl.name}});
    }
    else if (node->flags & flgMain) {
        format(ctx->state, " cxy_main", NULL);
    }
    else {
        format(ctx->state, " ", NULL);
        if (hasFlag(node, Generated))
            writeDeclNamespace(ctx, node->type->namespace, NULL);
        else
            writeNamespace(ctx, NULL);

        format(ctx->state, "{s}", (FormatArg[]){{.s = node->funcDecl.name}});
    }

    if (isMember) {
        format(ctx->state, "(", NULL);
        if (node->type->flags & flgConst)
            format(ctx->state, "const ", NULL);

        writeTypename(ctx, parent->type);
        format(ctx->state, " *this", NULL);

        if (node->funcDecl.params)
            format(ctx->state, ", ", NULL);

        generateManyAstsWithDelim(
            visitor, "", ", ", ")", node->funcDecl.params);
    }
    else {
        generateManyAstsWithDelim(
            visitor, "(", ", ", ")", node->funcDecl.params);
    }

    format(ctx->state, " ", NULL);
    if (node->funcDecl.body->tag == astBlockStmt) {
        astConstVisit(visitor, node->funcDecl.body);
    }
    else {
        format(ctx->state, "{{{>}\n", NULL);
        if (node->type->func.retType != makeVoidType(table)) {
            format(ctx->state, "return ", NULL);
        }
        astConstVisit(visitor, node->funcDecl.body);
        format(ctx->state, ";", NULL);
        format(ctx->state, "{<}\n}", NULL);
    }

    ctx->namespace = namespace;
}

void generateFuncDeclaration(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    const AstNode *parent = type->func.decl->parentScope;

    format(state, ";\n", NULL);
    generateTypeUsage(context, type->func.retType);
    if (typeIs(type->func.retType, This))
        format(context->state, " *", NULL);
    else
        format(state, " ", NULL);
    writeTypename(context, parent->type);
    format(state, "__{s}", (FormatArg[]){{.s = type->name}});
    format(state, "(", NULL);
    if (type->flags & flgConst)
        format(state, "const ", NULL);
    writeTypename(context, parent->type);
    format(state, " *", NULL);

    for (u64 i = 0; i < type->func.paramsCount; i++) {
        format(state, ", ", NULL);
        generateTypeUsage(context, type->func.params[i]);
    }
    format(state, ")", NULL);
}

void generateFunctionTypedef(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    const AstNode *parent =
        type->func.decl ? type->func.decl->parentScope : NULL;
    bool isMember = parent && parent->tag == astStructDecl;

    format(state, "typedef ", NULL);
    generateTypeUsage(context, type->func.retType);
    format(state, "(*", NULL);
    if (isMember) {
        writeTypename(context, parent->type);
        format(state, "__", NULL);
    }
    writeTypename(context, type);
    format(state, ")(", NULL);
    if (isMember) {
        if (type->flags & flgConst)
            format(state, "const ", NULL);
        writeTypename(context, parent->type);
        format(state, " *this", NULL);
    }

    for (u64 i = 0; i < type->func.paramsCount; i++) {
        if (isMember || i != 0)
            format(state, ", ", NULL);
        generateTypeUsage(context, type->func.params[i]);
    }
    format(state, ")", NULL);
    if (isMember)
        generateFuncDeclaration(context, type);
}

const Type *checkMethodDeclSignature(AstVisitor *visitor, AstNode *node)
{
    const Type *ret = NULL, **params, *type = NULL;
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    u64 paramsCount = 0;
    bool withDefaultValues = false;

    defineSymbol(ctx->env, ctx->L, node->funcDecl.name, node);
    if (node->funcDecl.operatorOverload != opInvalid) {
        if (!validateOperatorOverloadFunc(ctx, node)) {
            return node->type = ERROR_TYPE(ctx);
        }
    }

    pushScope(ctx->env, node);
    params =
        checkFunctionParams(visitor, node, &paramsCount, &withDefaultValues);

    ret = makeAutoType(ctx->typeTable);
    if (node->funcDecl.ret)
        ret = evalType(visitor, node->funcDecl.ret);

    node->type = makeFunctionDeclType(
        ctx, node, ret, params, paramsCount, withDefaultValues);

    free((void *)params);
    popScope(ctx->env);

    return node->type;
}

void checkMethodDeclBody(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *ret = NULL, *type = NULL;
    AstNode *param = node->funcDecl.params;
    csAssert0(node->parentScope && nodeIs(node->parentScope, StructDecl));

    const Type *parent = typeIs(node->parentScope->type, This)
                             ? node->parentScope->type
                             : makePointerType(ctx->typeTable,
                                               node->parentScope->type,
                                               node->parentScope->type->flags);

    const AstNode *lastReturn = ctx->lastReturn;
    ctx->lastReturn = NULL;

    pushScope(ctx->env, node);
    defineSymbol(
        ctx->env,
        ctx->L,
        "this",
        makeAstNode(ctx->pool,
                    &node->loc,
                    &(AstNode){.tag = astIdentifier,
                               .flags = (parent->flags & ~flgTopLevelDecl),
                               .type = parent,
                               .ident.value = "this"}));

    if (node->parentScope->structDecl.base) {
        defineSymbol(
            ctx->env,
            ctx->L,
            "super",
            makeAstNode(
                ctx->pool,
                &node->loc,
                &(AstNode){.tag = astIdentifier,
                           .flags = parent->flags | flgAddThis,
                           .type = node->parentScope->structDecl.base->type,
                           .ident.value = "super"}));
    }

    for (; param; param = param->next) {
        defineSymbol(ctx->env, ctx->L, param->funcParam.name, param);
    }

    node->funcDecl.body->parentScope = node;
    ret = evalType(visitor, node->funcDecl.body);

    if (ctx->lastReturn && typeIs(ret, Void))
        ret = ctx->lastReturn->type;
    ctx->lastReturn = lastReturn;

    if (node->type->func.retType != ret) {
        type = node->type;
        node->type = makeFunctionDeclType(ctx,
                                          node,
                                          ret,
                                          type->func.params,
                                          type->func.paramsCount,
                                          type->func.defaultValuesCount);
        removeFromTypeTable(ctx->typeTable, type);
    }

    popScope(ctx->env);
}

void checkFuncParam(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    if (node->parentScope == NULL || node->parentScope->tag != astFuncType)
        defineSymbol(ctx->env, ctx->L, node->funcParam.name, node);

    if (node->funcParam.type)
        node->type = evalType(visitor, node->funcParam.type);
    else
        csAssert0(node->type);

    if (node->funcParam.def) {
        const Type *def = evalType(visitor, node->funcParam.def);
        if (!isTypeAssignableFrom(node->type, def)) {
            logError(ctx->L,
                     &node->funcParam.def->loc,
                     "parameter default value type '{t}' not compatible with "
                     "parameter type '{t}",
                     (FormatArg[]){{.t = def}, {.t = node->type}});
        }
    }
}

void checkFunctionDecl(AstVisitor *visitor, AstNode *node)
{
    const Type *ret = NULL, **params, *type = NULL;
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const AstNode *lastReturn = ctx->lastReturn;
    ctx->lastReturn = NULL;

    u64 paramsCount = 0;
    bool withDefaultValues = false;

    defineSymbol(ctx->env, ctx->L, node->funcDecl.name, node);
    addModuleExport(ctx, node, node->funcDecl.name);
    defineDeclarationAliasName(ctx, node);

    if (hasFlag(node, Native)) {
        const AstNode *alias = findAttribute(node, "alias");
        if (alias) {
        }
    }

    if (!ctx->mainOptimized) {
        node->flags |=
            (strcmp(node->funcDecl.name, "main") == 0) ? flgMain : flgNone;
        ctx->mainOptimized = node->flags & flgMain;
    }

    pushScope(ctx->env, node);

    params =
        checkFunctionParams(visitor, node, &paramsCount, &withDefaultValues);

    ret = makeAutoType(ctx->typeTable);
    if (node->funcDecl.ret)
        ret = evalType(visitor, node->funcDecl.ret);

    type = makeFunctionDeclType(
        ctx, node, ret, params, paramsCount, withDefaultValues);
    node->type = type;

    if (node->funcDecl.body) {
        node->funcDecl.body->parentScope = node;
        ret = evalType(visitor, node->funcDecl.body);

        if (ctx->lastReturn && typeIs(ret, Void)) {
            ret = ctx->lastReturn->type;
        }
    }

    if (type->func.retType != ret) {
        node->type = makeFunctionDeclType(
            ctx, node, ret, params, paramsCount, withDefaultValues);
        removeFromTypeTable(ctx->typeTable, type);
    }

    ctx->lastReturn = lastReturn;

    free((void *)params);
    popScope(ctx->env);
}

void checkFuncType(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    const Type *ret = evalType(visitor, node->funcType.ret);
    u64 count = countAstNodes(node->funcType.params);
    const Type **params = mallocOrDie(sizeof(Type *) * count);

    AstNode *param = node->funcType.params;
    for (u64 i = 0; param; param = param->next, i++) {
        param->parentScope = node;
        params[i] = evalType(visitor, param);
        if (params[i] == ERROR_TYPE(ctx))
            node->type = ERROR_TYPE(ctx);
    }

    if (node->type == NULL)
        node->type = makeFuncType(ctx->typeTable,
                                  &(Type){.tag = typFunc,
                                          .name = NULL,
                                          .flags = node->flags,
                                          .func = {.retType = ret,
                                                   .params = params,
                                                   .paramsCount = count,
                                                   .decl = node}});

    free(params);
}
