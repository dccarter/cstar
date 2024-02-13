//
// Created by Carter on 2023-08-27.
//
#include "check.h"

#include "../builtins.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"

#include "core/alloc.h"

static cstring pushGenericDeclNamespace(TypeTable *types, const AstNode *decl)
{
    cstring namespace = types->currentNamespace;
    AstNode *parent = decl->parentScope;
    if (!nodeIs(parent, Program) || parent->program.module == NULL) {
        types->currentNamespace = NULL;
    }
    else
        types->currentNamespace = parent->program.module->moduleDecl.name;
    return namespace;
}

static bool inferGenericFunctionTypes(AstVisitor *visitor,
                                      const AstNode *generic,
                                      const Type **paramTypes,
                                      u64 index)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    csAssert0(ctx->currentCall);
    AstNode *call = ctx->currentCall, *arg = call->callExpr.args,
            *params = generic->genericDecl.params,
            *param = getNodeAtIndex(params, index);

    u64 argsCount = countAstNodes(arg);

    if (argsCount == 0)
        return false;

    const Type **argTypes = mallocOrDie(sizeof(Type *) * argsCount);
    bool status = true;
    for (u64 i = 0; arg; arg = arg->next, i++) {
        argTypes[i] = checkType(visitor, arg);
        if (typeIs(argTypes[i], Error))
            status = false;
    }

    if (!status) {
        free(argTypes);
        return false;
    }

    for (; param; param = param->next, index++) {
        if (param->genericParam.inferIndex > argsCount) {
            csAssert0(hasFlag(param, Variadic));
            paramTypes[index] =
                makeTupleType(ctx->types, NULL, 0, param->flags);
            continue;
        }

        paramTypes[index] = argTypes[param->genericParam.inferIndex - 1];
    }

    free(argTypes);
    return true;
}

static bool transformVariadicFunctionCallArgs(AstVisitor *visitor,
                                              const AstNode *func,
                                              u64 flags)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *args = ctx->currentCall->callExpr.args;

    u64 requiredCount = func->funcDecl.requiredParamsCount,
        totalParams = func->funcDecl.paramsCount;
    u64 argsCount = countAstNodes(args);

    if (argsCount < requiredCount) {
        logError(ctx->L,
                 &ctx->currentCall->loc,
                 "insufficient number of arguments passed, expecting at least "
                 "'{u64}' got '{u64}",
                 (FormatArg[]){{.u64 = requiredCount}, {.u64 = argsCount}});
        logNote(ctx->L, &func->loc, "function declared here", NULL);
        return false;
    }

    if (argsCount >= totalParams) {
        AstNode *last = getLastAstNode(args);
        checkType(visitor, last);

        if (typeIs(last->type, Error))
            return false;

        if (hasFlag(last, Variadic)) {
            logError(ctx->L,
                     &last->loc,
                     "passing a variadic function parameter to a "
                     "generic/variadic "
                     "function not supported, consider using spread `...` "
                     "operator",
                     NULL);
            return false;
        }

        if (totalParams == 1) {
            if (nodeIs(args, SpreadExpr) && args->next == NULL) {
                ctx->currentCall->callExpr.args->next = args->spreadExpr.expr;
            }
            else {
                *ctx->currentCall->callExpr.args = (AstNode){
                    .tag = astTupleExpr,
                    .flags = flgVariadic | flags,
                    .loc = *manyNodesLoc(args),
                    .tupleExpr = {.elements = duplicateAstNode(ctx->pool, args),
                                  .len = 1 + (argsCount - totalParams)}};
            }
        }
        else {
            AstNode *variadic = getNodeAtIndex(args, totalParams - 2);
            args = variadic->next;
            if (nodeIs(args, SpreadExpr) && args->next == NULL) {
                variadic->next = args->spreadExpr.expr;
            }
            else {
                variadic->next = makeAstNode(
                    ctx->pool,
                    manyNodesLoc(args),
                    &(AstNode){
                        .tag = astTupleExpr,
                        .flags = flgVariadic | flags,
                        .tupleExpr = {.elements = args,
                                      .len = 1 + (argsCount - totalParams)}});
            }
        }
    }

    return true;
}

static void removeEmptyVariadicFunctionParameter(AstNode *node)
{
    if (node->funcDecl.signature->params->next != NULL) {
        AstNode *prev = node->funcDecl.signature->params;
        AstNode *it = prev->next;
        while (it->next) {
            prev = it;
            it = it->next;
        }
        prev->next = NULL;
    }
}

const Type *resolveGenericDecl(AstVisitor *visitor,
                               const AstNode *generic,
                               AstNode *node)
{

    TypingContext *ctx = getAstVisitorContext(visitor);

    if (hasFlag(generic, Variadic)) {
        const AstNode *decl = generic->genericDecl.decl;
        // transform function call params
        if (!transformVariadicFunctionCallArgs(
                visitor, decl, generic->flags & flgTransient)) //
        {
            return node->type = ERROR_TYPE(ctx);
        }
    }

    const Type *type = generic->type;
    AstNode *args = node->pathElement.args, *arg = args;
    u64 index = 0, argsCount = countAstNodes(args),
        paramsCount = type->generic.paramsCount;
    const Type **paramTypes = NULL;

    if (argsCount > paramsCount)
        return node->type = ERROR_TYPE(ctx);

    paramTypes = mallocOrDie(sizeof(Type *) * paramsCount);
    for (; arg; index++, arg = arg->next) {
        paramTypes[index] = checkType(visitor, arg);
    }

    if (index < paramsCount) {
        // maybe inferred arguments?
        if ((index < generic->genericDecl.inferrable) ||  //
            !nodeIs(generic->genericDecl.decl, FuncDecl)) //
        {
            AstNode *it = getNodeAtIndex(generic->genericDecl.params, index);
            logError(ctx->L,
                     &(args ?: node)->loc,
                     "missing argument for type parameter '{s}'",
                     (FormatArg[]){{.s = it->genericParam.name}});
            logNote(ctx->L, &it->loc, "generic parameter declared here", NULL);
            goto resolveGenericDeclError;
        }

        if (!inferGenericFunctionTypes(visitor, generic, paramTypes, index)) {
            // infers generic function types
            goto resolveGenericDeclError;
        }
    }

    GetOrInset goi =
        makeAppliedType(ctx->types,
                        &(Type){.tag = typApplied,
                                .applied = {.args = paramTypes,
                                            .totalArgsCount = paramsCount,
                                            .argsCount = paramsCount,
                                            .from = type}});
    free(paramTypes);

    if (goi.f) {
        if (nodeIs(node, PathElem)) {
            node->type = goi.s->applied.decl->type;
            node->pathElement.resolvesTo = goi.s->applied.decl;
            node->pathElement.name = getDeclarationName(goi.s->applied.decl);
            node->pathElement.args = NULL;
        }
        else {
            node->type = goi.s->applied.decl->type;
            node->ident.resolvesTo = goi.s->applied.decl;
            node->ident.value = getDeclarationName(goi.s->applied.decl);
        }
        return node->type;
    }

    AstNode *substitute = cloneGenericDeclaration(ctx->pool, generic),
            *param = getGenericDeclarationParams(substitute);
    substitute->flags |= flgGenerated;
    substitute->flags &= ~flgVariadic;

    cstring name =
        makeAnonymousVariable(ctx->strings, getDeclarationName(substitute));
    setDeclarationName(substitute, name);

    cstring namespace = pushGenericDeclNamespace(ctx->types, generic);
    type = goi.s;
    ((Type *)type)->applied.decl = substitute;
    bool isEmptyTuple = false;
    for (u64 i = 0; param; param = param->next, i++) {
        param->type = type->applied.args[i];
        isEmptyTuple =
            typeIs(param->type, Tuple) && param->type->tuple.count == 0;
    }

    if (nodeIs(substitute, StructDecl) && hasFlag(generic, Builtin)) {
        if (getDeclarationName(generic) == S___Optional)
            substitute->flags |= flgOptional;
        else if (getDeclarationName(generic) == S_Slice)
            substitute->flags |= flgSlice;
    }

    if (nodeIs(substitute, FuncDecl)) {
        if (hasFlag(substitute, Variadic) && isEmptyTuple)
            removeEmptyVariadicFunctionParameter(substitute);

        // check signature first in case there is recursion in the body
        node->type = checkFunctionSignature(visitor, substitute);
        if (substitute->funcDecl.this_) {
            AstNode *parent = substitute->parentScope;
            csAssert0(parent && parent->type);
            substitute->funcDecl.this_->type =
                nodeIs(parent, ClassDecl)
                    ? parent->type
                    : makePointerType(ctx->types,
                                      parent->type,
                                      substitute->flags & flgConst);
            substitute->funcDecl.signature->params = substitute->funcDecl.this_;
            substitute->funcDecl.this_ = NULL;
            node->flags |= flgAddThis;
        }
        if (!typeIs(node->type, Error))
            node->type = checkFunctionBody(visitor, substitute);
    }
    else {
        node->type = checkType(visitor, substitute);
        substitute->flags |= flgVisited;
    }

    addTopLevelDeclaration(ctx, substitute);

    if (nodeIs(node, PathElem)) {
        node->pathElement.resolvesTo = substitute;
        node->pathElement.name = getDeclarationName(substitute);
        node->pathElement.args = NULL;
    }
    else {
        node->ident.resolvesTo = substitute;
        node->ident.value = getDeclarationName(substitute);
    }

    ctx->types->currentNamespace = namespace;
    ((Type *)substitute->type)->from = type;
    return substitute->type;

resolveGenericDeclError:
    free(paramTypes);
    logError(ctx->L,
             &node->loc,
             "resolving generic declaration failed '{t}' failed",
             (FormatArg[]){{.t = generic->type}});
    return node->type = ERROR_TYPE(ctx);
}

void checkGenericDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    attr(unused) bool inferrable = false;
    node->type = makeGenericType(ctx->types, node);
}
