//
// Created by Carter on 2023-08-27.
//
#include "check.h"

#include "../builtins.h"
#include "../mangle.h"

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

static bool resolveGenericDeclDefaults(AstVisitor *visitor,
                                       const AstNode *generic,
                                       const Type **paramTypes,
                                       u64 index)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *params = generic->genericDecl.params, *param = params;
    MemPool pool = newMemPool();
    CloneAstConfig config = {
        .pool = &pool, .createMapping = true, .root = generic};

    initCloneAstNodeMapping(&config);

    u64 i = 0;
    bool status = true;
    for (; param; param = param->next, i++) {
        if (i >= index) {
            AstNode *node =
                cloneAstNode(&config, param->genericParam.defaultValue);
            paramTypes[i] = checkType(visitor, node);
            if (typeIs(paramTypes[i], Error))
                status = false;
        }
        cloneAstNode(&config, param)->type = paramTypes[i];
    }

    deinitCloneAstNodeConfig(&config);
    freeMemPool(&pool);
    return status;
}

static const Type *closureArgToClosureParamType(TypingContext *ctx,
                                                const FileLoc *loc,
                                                const Type *argType)
{
    const Type *opCall = findStructMemberType(argType, S_CallOverload);
    const Type **funcTypeParams =
        mallocOrDie(sizeof(Type *) * (opCall->func.paramsCount + 1));
    funcTypeParams[0] = makeVoidPointerType(ctx->types, flgNone);
    AstNodeList funcTypeDeclParams = {};
    insertAstNode(
        &funcTypeDeclParams,
        makeFunctionParam(
            ctx->pool,
            loc,
            NULL,
            makeTypeReferenceNode(
                ctx->pool, makeVoidPointerType(ctx->types, flgNone), loc),
            NULL,
            flgNone,
            NULL));

    for (u64 i = 0; i < opCall->func.paramsCount; i++) {
        funcTypeParams[i + 1] = opCall->func.params[i];
        insertAstNode(
            &funcTypeDeclParams,
            makeFunctionParam(
                ctx->pool,
                loc,
                NULL,
                makeTypeReferenceNode(ctx->pool, funcTypeParams[i + 1], loc),
                NULL,
                funcTypeParams[i + 1]->flags,
                NULL));
    }

    AstNode *funcTypeDecl = makeFunctionType(
        ctx->pool,
        loc,
        funcTypeDeclParams.first,
        makeTypeReferenceNode(ctx->pool, opCall->func.retType, loc),
        opCall->flags,
        NULL,
        NULL);
    funcTypeDecl->type = makeFuncType(
        ctx->types,
        &(Type){.tag = typFunc,
                .func = {.params = funcTypeParams,
                         .paramsCount = opCall->func.paramsCount + 1,
                         .retType = opCall->func.retType,
                         .decl = funcTypeDecl}});

    const Type *paramType =
        makeTupleType(ctx->types,
                      (const Type *[]){makeVoidPointerType(ctx->types, flgNone),
                                       funcTypeDecl->type},
                      2,
                      flgFuncTypeParam | flgReferenceMembers);
    free(funcTypeParams);
    return paramType;
}

static bool inferGenericFunctionTypes(AstVisitor *visitor,
                                      const AstNode *generic,
                                      const Type **paramTypes,
                                      u64 index)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    csAssert0(ctx->currentCall);
    AstNode *decl = generic->genericDecl.decl, *call = ctx->currentCall,
            *arg = call->callExpr.args, *params = generic->genericDecl.params,
            *param = getNodeAtIndex(params, index);

    u64 argsCount = countAstNodes(arg);
    if (argsCount < decl->funcDecl.requiredParamsCount) {
        if (!hasFlag(generic, Variadic))
            return false;
    }

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
        if (hasFlag(param, Variadic)) {
            arg = getNodeAtIndex(call->callExpr.args,
                                 param->genericParam.inferIndex - 1);
            if (arg == NULL) {
                paramTypes[index] = makeVoidType(ctx->types);
                break;
            }

            while (arg) {
                if (hasFlag(arg->type, Closure)) {
                    paramTypes[index++] = closureArgToClosureParamType(
                        ctx, &param->loc, arg->type);
                    implementTupleTypeCopyAndDestructor(visitor, arg);
                }
                else
                    paramTypes[index++] = arg->type;
                arg = arg->next;
            }
            break;
        }

        if (hasFlag(argTypes[param->genericParam.inferIndex - 1], Closure)) {
            paramTypes[index] = closureArgToClosureParamType(
                ctx, &param->loc, argTypes[param->genericParam.inferIndex - 1]);
        }
        else {
            const Type *inferred = argTypes[param->genericParam.inferIndex - 1];
            if (param->genericParam.innerType) {
                if (typeIs(inferred, Reference))
                    paramTypes[index] = inferred->reference.referred;
                else if (typeIs(inferred, Pointer))
                    paramTypes[index] = inferred->pointer.pointed;
                else
                    paramTypes[index] = inferred;
            }
            else {
                paramTypes[index] = inferred;
            }
            paramTypes[index] = maybeUnThisType(paramTypes[index]);
        }
    }

    free(argTypes);
    return true;
}

static bool transformVariadicFunctionCallArgs(AstVisitor *visitor,
                                              const AstNode *func,
                                              u64 *variadicArgsCount)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *args = ctx->currentCall->callExpr.args, *arg = args;
    while (arg) {
        checkType(visitor, arg);
        if (typeIs(arg->type, Error))
            return false;
        arg = arg->next;
    }

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
        *variadicArgsCount = argsCount - totalParams;
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
    else {
        if (node->funcDecl.this_)
            node->funcDecl.this_->next = NULL;
        node->funcDecl.signature->params = NULL;
    }
}

static void addVariadicParamsToSubstitute(TypingContext *ctx,
                                          const AstNode *generic,
                                          AstNode *substitute,
                                          const Type *type,
                                          u64 variadicArgumentsCount)
{
    const Type **paramTypes = type->applied.args;
    u64 offset = generic->genericDecl.paramsCount;
    AstNode *param = getLastAstNode(substitute->funcDecl.signature->params);
    if (typeIs(paramTypes[offset - 1], Void)) {
        param->type = paramTypes[offset - 1];
        removeEmptyVariadicFunctionParameter(substitute);
        return;
    }

    cstring prefix = param->funcParam.name;
    int j = 0;
    while (j < variadicArgumentsCount) {
        param->next = makeFunctionParam(
            ctx->pool,
            &param->loc,
            makeStringf(ctx->strings, "%s%d", prefix, j + 1),
            makeTypeReferenceNode(
                ctx->pool, paramTypes[offset + j], &param->loc),
            NULL,
            flgNone,
            NULL);
        param = param->next;
        j++;
    }
}

const Type *resolveGenericDecl(AstVisitor *visitor,
                               const AstNode *generic,
                               AstNode *node)
{

    TypingContext *ctx = getAstVisitorContext(visitor);
    u64 variadicArgumentsCount = 0;
    if (hasFlag(generic, Variadic)) {
        const AstNode *decl = generic->genericDecl.decl;
        // transform function call params
        if (!transformVariadicFunctionCallArgs(
                visitor, decl, &variadicArgumentsCount)) //
        {
            return node->type = ERROR_TYPE(ctx);
        }
    }

    const Type *type = generic->type;
    AstNode *args = node->pathElement.args, *arg = args;
    u64 index = 0, argsCount = countAstNodes(args),
        paramsCount = type->generic.paramsCount + variadicArgumentsCount;
    const Type **paramTypes = NULL;

    if (argsCount > paramsCount)
        return node->type = ERROR_TYPE(ctx);

    paramTypes = mallocOrDie(sizeof(Type *) * paramsCount);
    for (; arg; index++, arg = arg->next) {
        paramTypes[index] = maybeUnThisType(checkType(visitor, arg));
    }

    if (index < paramsCount) {
        AstNode *it = getNodeAtIndex(generic->genericDecl.params, index);
        bool isFuncDecl = nodeIs(generic->genericDecl.decl, FuncDecl);
        // maybe inferred arguments?
        if (!isFuncDecl && it->genericParam.defaultValue) {
            if (!resolveGenericDeclDefaults(
                    visitor, generic, paramTypes, index)) {

                goto resolveGenericDeclError;
            }
        }
        else if (isFuncDecl && (index >= generic->genericDecl.inferrable)) {
            if (!inferGenericFunctionTypes(
                    visitor, generic, paramTypes, index)) {
                // infers generic function types
                goto resolveGenericDeclError;
            }
        }
        else {
            logError(ctx->L,
                     &(args ?: node)->loc,
                     "missing argument for type parameter '{s}'",
                     (FormatArg[]){{.s = it->genericParam.name}});
            logNote(ctx->L, &it->loc, "generic parameter declared here", NULL);
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
        if (typeIs(node->type, Func) &&
            getMemberFunctionThis(node->type->func.decl))
            node->flags |= flgAddThis;
        return node->type;
    }

    AstNode *substitute = cloneGenericDeclaration(ctx->pool, generic),
            *param = getGenericDeclarationParams(substitute);
    substitute->flags |= flgGenerated;
    substitute->flags &= ~flgVariadic;

    cstring name = makeMangledName(ctx->strings,
                                   getDeclarationName(substitute),
                                   goi.s->applied.args,
                                   goi.s->applied.argsCount,
                                   hasFlag(substitute, Const));
    setDeclarationName(substitute, name);

    cstring namespace = pushGenericDeclNamespace(ctx->types, generic);
    type = goi.s;
    ((Type *)type)->applied.decl = substitute;
    for (u64 i = 0; param; param = param->next, i++) {
        param->type = type->applied.args[i];
        if (hasFlag(param, Variadic)) {
            csAssert0(param->next == NULL);
            int j = 1;
            cstring prefix = param->genericParam.name;
            while (++i < type->applied.argsCount) {
                param->next = makeAstNode(
                    ctx->pool,
                    &param->loc,
                    &(AstNode){.tag = astGenericParam,
                               .type = type->applied.args[i],
                               .genericParam = {
                                   .name = makeStringf(
                                       ctx->strings, "%s%u", prefix, j++)}});
                param = param->next;
            }
        }
    }

    if (nodeIs(substitute, StructDecl) && hasFlag(generic, Builtin)) {
        if (getDeclarationName(generic) == S_Optional)
            substitute->flags |= flgOptional;
        else if (getDeclarationName(generic) == S_Slice)
            substitute->flags |= flgSlice;
    }

    if (nodeIs(substitute, FuncDecl)) {
        if (hasFlag(generic, Variadic)) {
            addVariadicParamsToSubstitute(
                ctx, generic, substitute, type, variadicArgumentsCount);
        }

        // check signature first in case there is recursion in the body
        node->type = checkFunctionSignature(visitor, substitute);
        if (substitute->funcDecl.this_) {
            AstNode *parent = substitute->parentScope;
            csAssert0(parent && parent->type);
            substitute->funcDecl.this_->type =
                nodeIs(parent, ClassDecl)
                    ? makeReferenceType(ctx->types,
                                        parent->type,
                                        substitute->flags & flgConst)
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

    astModifierAdd(&ctx->root, substitute);

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
