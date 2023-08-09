//
// Created by Carter Mbotho on 2023-07-14.
//

#include "check.h"

#include "lang/ast.h"
#include "lang/codec.h"
#include "lang/flag.h"
#include "lang/operations.h"
#include "lang/strings.h"
#include "lang/ttable.h"
#include "lang/types.h"
#include "lang/visitor.h"

#include "core/alloc.h"

const Type *checkType(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    if (node == NULL)
        return NULL;

    if (node->type)
        return node->type;

    astVisit(visitor, node);
    return resolveType(node->type);
}

static const FileLoc *manyNodesLoc(FileLoc *dst, AstNode *nodes)
{
    if (nodes == NULL)
        return NULL;

    if (nodes->next == NULL) {
        *dst = nodes->loc;
        return dst;
    }

    return locExtend(dst, &nodes->loc, &getLastAstNode(nodes)->loc);
}

static const FileLoc *lastNodeLoc(FileLoc *dst, AstNode *nodes)
{
    if (nodes == NULL)
        return NULL;
    *dst = getLastAstNode(nodes)->loc;
    return dst;
}

static void addTopLevelDeclaration(TypingContext *ctx, AstNode *node)
{
    csAssert0(ctx->root.current);

    node->next = ctx->root.current;
    if (ctx->root.previous)
        ctx->root.previous->next = node;
    else
        ctx->root.program->program.decls = node;
    ctx->root.previous = node;
}

static void addBlockLevelDeclaration(TypingContext *ctx, AstNode *node)
{
    csAssert0(ctx->block.current);

    node->next = ctx->block.current;
    if (ctx->block.previous)
        ctx->block.previous->next = node;
    else
        ctx->block.self->program.decls = node;
    ctx->block.previous = node;
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
            paramTypes[index] = makeTupleType(ctx->types, NULL, 0, flgNone);
            continue;
        }

        paramTypes[index] = argTypes[param->genericParam.inferIndex - 1];
    }

    free(argTypes);
    return true;
}

static bool transformVariadicFunctionCallArgs(AstVisitor *visitor,
                                              const AstNode *func)
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

        if (typeIs(last, Error))
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
                    .flags = flgVariadic,
                    .loc = *manyNodesLoc(&(FileLoc){}, args),
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
                    manyNodesLoc(&(FileLoc){}, args),
                    &(AstNode){
                        .tag = astTupleExpr,
                        .flags = flgVariadic,
                        .tupleExpr = {.elements = args,
                                      .len = 1 + (argsCount - totalParams)}});
            }
        }
    }

    return true;
}

static AstNode *makeSpreadVariable(TypingContext *ctx, AstNode *expr)
{
    if (nodeIs(expr, Identifier) ||
        (nodeIs(expr, Path) && expr->path.elements->next == NULL))
        return expr;

    // Create variable for this
    return makeAstNode(
        ctx->pool,
        &expr->loc,
        &(AstNode){
            .tag = astVarDecl,
            .type = expr->type,
            .flags = expr->flags,
            .varDecl = {.names = makeGenIdent(
                            ctx->pool, ctx->strings, &expr->loc, expr->type),
                        .init = expr}});
}

static void removeEmptyVariadicFunctionParameter(AstNode *node)
{
    if (node->funcDecl.signature->params->next == NULL)
        node->funcDecl.signature->params = NULL;
    else {
        AstNode *prev = node->funcDecl.signature->params;
        AstNode *it = prev->next;
        while (it->next) {
            prev = it;
            it = it->next;
        }
        prev->next = NULL;
    }
}

static const Type *resolveGenericDecl(AstVisitor *visitor,
                                      const AstNode *generic,
                                      AstNode *node)
{

    TypingContext *ctx = getAstVisitorContext(visitor);

    if (hasFlag(generic, Variadic)) {
        const AstNode *decl = generic->genericDecl.decl;
        // transform function call params
        if (!transformVariadicFunctionCallArgs(visitor, decl)) {
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
        node->type = goi.s->applied.decl->type;
        node->pathElement.resolvesTo = goi.s->applied.decl;
        node->pathElement.name = getDeclarationName(goi.s->applied.decl);
        node->pathElement.args = NULL;
        return node->type;
    }

    AstNode *substitute = cloneGenericDeclaration(ctx->pool, generic),
            *param = getGenericDeclarationParams(substitute);
    cstring name =
        makeAnonymousVariable(ctx->strings, getDeclarationName(substitute));
    setDeclarationName(substitute, name);
    addTopLevelDeclaration(ctx, substitute);

    type = goi.s;
    ((Type *)type)->applied.decl = substitute;
    bool isEmptyTuple = false;
    for (u64 i = 0; param; param = param->next, i++) {
        param->type = type->applied.args[i];
        isEmptyTuple =
            typeIs(param->type, Tuple) && param->type->tuple.count == 0;
    }

    if (nodeIs(substitute, FuncDecl)) {
        if (hasFlag(substitute, Variadic) && isEmptyTuple)
            removeEmptyVariadicFunctionParameter(substitute);

        // check signature first in case there is recursion in the body
        node->type = checkFunctionSignature(visitor, substitute);
        if (!typeIs(node->type, Error))
            node->type = checkFunctionBody(visitor, substitute);
    }
    else
        node->type = checkType(visitor, substitute);

    node->pathElement.resolvesTo = substitute;
    node->pathElement.name = getDeclarationName(substitute);
    node->pathElement.args = NULL;

    return substitute->type;

resolveGenericDeclError:
    free(paramTypes);
    return node->type = ERROR_TYPE(ctx);
}

const Type *matchOverloadedFunction(TypingContext *ctx,
                                    const Type *callee,
                                    const Type **argTypes,
                                    u64 argsCount,
                                    const FileLoc *loc,
                                    u64 flags)
{
    AstNode *decls = callee->func.decl->list.first ?: callee->func.decl,
            *decl = decls;
    const Type *found = NULL;
    u64 maxScore = argsCount * 2, matchScore = 0, declarations = 0;

    for (; decl; decl = decl->list.link) {
        const Type *type = decl->type;
        if (type == NULL)
            continue;

        if ((decl->flags & flags) != flags)
            continue;

        u64 defaultCount = type->func.defaultValuesCount,
            paramsCount = type->func.paramsCount,
            requiredCount = paramsCount - defaultCount;

        declarations++;

        if (argsCount < requiredCount || argsCount > paramsCount)
            continue;

        u64 score = maxScore;
        bool compatible = true;
        for (u64 i = 0; i < argsCount; i++) {
            const Type *paramType = type->func.params[i];
            compatible = paramType == argTypes[i];
            if (!compatible) {
                compatible = isTypeAssignableFrom(paramType, argTypes[i]);
                if (compatible) {
                    score--;
                    continue;
                }
                break;
            }
        }

        if (!compatible)
            continue;

        if (score == maxScore)
            return type;

        if (score >= matchScore) {
            matchScore = score;
            found = type;
        }
    }

    if (found)
        return found;

    if (loc) {
        Type type = {.tag = typFunc,
                     .flags = flags,
                     .func = {.params = argTypes,
                              .paramsCount = argsCount,
                              .retType = makeAutoType(ctx->types)}};

        logError(
            ctx->L,
            loc,
            "incompatible function reference ({u64} functions declared did "
            "not match function with signature {t})",
            (FormatArg[]){{.u64 = declarations}, {.t = &type}});

        decl = decls;
        while (decl) {
            logError(ctx->L,
                     &decl->loc,
                     "found declaration with incompatible signature {t}",
                     (FormatArg[]){{.t = decl->type}});
            decl = decl->list.link;
        }
    }
    return NULL;
}

static void checkFunctionCallEpilogue(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *callee = node->callExpr.callee, *args = node->callExpr.args;
    const Type *callee_ = NULL;

    u64 argsCount = countAstNodes(args);
    const Type **argTypes = mallocOrDie(sizeof(Type *) * argsCount);
    AstNode *arg = args;
    for (u64 i = 0; arg; arg = arg->next, i++) {
        argTypes[i] = arg->type ?: checkType(visitor, arg);
        if (typeIs(argTypes[i], Error))
            node->type = ERROR_TYPE(ctx);
    }

    if (typeIs(node->type, Error)) {
        free(argTypes);
        return;
    }

    callee_ = matchOverloadedFunction(
        ctx, callee->type, argTypes, argsCount, &node->loc, flgNone);
    free(argTypes);

    if (callee_ == NULL || typeIs(callee_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    node->type = callee_->func.retType;
    if (typeIs(node->type, Auto)) {
        logError(ctx->L,
                 &callee->loc,
                 "call of function '{t}' returning a deducted type before the "
                 "return type is deduced",
                 (FormatArg[]){{.t = callee_}});
        logNote(ctx->L,
                &callee_->func.decl->loc,
                "maybe add a return type to function '{s}' or declare it "
                "before call site",
                (FormatArg[]){{.s = callee_->func.name}});
        node->type = ERROR_TYPE(ctx);
    }
}

static void checkTypeRef(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    csAssert0(node->type);
}

static void checkLiteral(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    switch (node->tag) {
    case astNullLit:
        node->type = makeVoidPointerType(ctx->types, flgNone);
        break;
    case astBoolLit:
        node->type = getPrimitiveType(ctx->types, prtBool);
        break;
    case astCharLit:
        node->type = getPrimitiveType(ctx->types, prtChar);
        break;
    case astIntegerLit:
        node->type =
            getIntegerTypeForLiteral(ctx->types, integerLiteralValue(node));
        break;
    case astFloatLit:
        node->type = getPrimitiveType(ctx->types, prtF64);
        break;
    case astStringLit:
        node->type = makeStringType(ctx->types);
        break;
    default:
        unreachable("NOT LITERAL");
    }
}

static void checkBuiltinType(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    switch (node->tag) {
    case astAutoType:
        node->type = makeAutoType(ctx->types);
        break;
    case astStringType:
        node->type = makeStringType(ctx->types);
        break;
    case astVoidType:
        node->type = makeVoidType(ctx->types);
        break;
    case astPrimitiveType:
        node->type = getPrimitiveType(ctx->types, node->primitiveType.id);
        break;
    default:
        csAssert0(false);
    }
}

static void checkTupleType(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *elems = node->tupleType.elements, *elem = elems;
    u64 count = node->tupleType.len ?: countAstNodes(node->tupleType.elements);
    const Type **elems_ = mallocOrDie(sizeof(Type *) * count);

    for (u64 i = 0; elem; elem = elem->next, i++) {
        elems_[i] = checkType(visitor, elem);
        if (typeIs(elems_[i], Error))
            node->type = ERROR_TYPE(ctx);
    }

    if (typeIs(node->type, Error)) {
        free(elems_);
        return;
    }

    node->type =
        makeTupleType(ctx->types, elems_, count, node->flags & flgConst);

    free(elems_);
}

static void checkPointerType(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *pointed = checkType(visitor, node->pointerType.pointed);
    if (pointed == NULL || typeIs(pointed, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    node->type = makePointerType(ctx->types, pointed, node->flags & flgConst);
}

static void checkDefine(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *type = node->define.type, *container = node->define.container,
            *name = node->define.names;
    const Type *type_ = checkType(visitor, type);
    u64 count = 0;

    for (; name; name = name->next, count++) {
        name->type = type_;
    }

    node->type = type_;
    if (container == NULL || typeIs(type_, Error))
        return;

    cstring *names = mallocOrDie(sizeof(cstring) * count);
    name = node->define.names;
    for (u64 i = 0; name; name = name->next) {
        names[i] = name->ident.alias ?: name->ident.value;
    }
    qsort(names, count, sizeof(cstring), compareStrings);

    node->type = makeContainerType(
        ctx->types, container->ident.value, type_, names, count);

    free(names);
}

static void checkIdentifier(AstVisitor *visitor, AstNode *node)
{
    csAssert0(node->ident.resolvesTo);
    node->type = node->ident.resolvesTo->type;
}

const Type *checkPathElement(AstVisitor *visitor,
                             const Type *parent,
                             AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *resolved = NULL;
    switch (parent->tag) {
    case typStruct:
    case typInterface:
    case typContainer:
    case typModule:
    case typEnum:
        resolved = expectSymbolInType(
            ctx->types, parent, ctx->L, node->pathElement.name, &node->loc);
        break;
    default:
        logError(ctx->L,
                 &node->loc,
                 "expression of type '{t}' does not support member expressions",
                 (FormatArg[]){{.t = parent}});
        break;
    }

    if (resolved == NULL || typeIs(resolved, Error))
        return node->type = ERROR_TYPE(ctx);

    if (typeIs(resolved, Generic)) {
        resolved = resolveGenericDecl(visitor, resolved->generic.decl, node);
        if (typeIs(resolved, Error))
            return resolved;
    }

    node->type = resolved;
    node->flags = resolved->flags;
    return resolved;
}

static const Type *checkBasePathElement(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    if (node->pathElement.isKeyword) {
        cstring keyword = node->pathElement.name;
        csAssert0(node->pathElement.enclosure);
        AstNode *parent = getParentScope(node->pathElement.enclosure);
        if (keyword == S_super) {
            return parent->type->tStruct.base;
        }
        else if (keyword == S_this) {
            return parent->type;
        }
        else if (keyword == S_This) {
            return node->pathElement.enclosure->structDecl.thisType;
        }
        unreachable("unsupported keyword");
    }
    else {
        csAssert0(node->pathElement.resolvesTo);
        return node->type = node->pathElement.resolvesTo->type;
    }
}

static void checkPath(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *base = node->path.elements, *elem = base->next;
    if (hasFlag(node, Inherited)) {
        base->pathElement.resolvesTo =
            getBaseClassByName(ctx->currentStruct, base->pathElement.name);
    }
    const Type *type = checkBasePathElement(visitor, base);

    if (typeIs(type, Generic)) {
        type = resolveGenericDecl(visitor, base->pathElement.resolvesTo, base);
        if (type == NULL || typeIs(type, Error)) {
            node->type = type;
            return;
        }
    }

    u64 flags = base->flags | base->pathElement.resolvesTo->flags;
    for (; elem; elem = elem->next) {
        type = checkPathElement(visitor, stripAll(type), elem);
        if (typeIs(type, Error)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }
        flags = elem->flags;
    }

    node->type = type;
    node->flags = flags;
}

static void checkVarDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *type = node->varDecl.type, *init = node->varDecl.init;

    const Type *type_ =
        type ? checkType(visitor, type) : makeAutoType(ctx->types);
    const Type *init_ = checkType(visitor, init);

    if (typeIs(type_, Error) || typeIs(init_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (init_ == NULL) {
        node->type = type_;
        return;
    }

    if (!isTypeAssignableFrom(type_, init_)) {
        logError(ctx->L,
                 &node->loc,
                 "variable initializer of type '{t}' is not assignable to "
                 "variable type '{t}",
                 (FormatArg[]){{.t = init_}, {.t = type_}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    node->type = typeIs(type_, Auto) ? init_ : type_;
}

static void checkFunctionParam(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *type = node->funcParam.type, *def = node->funcParam.def;
    const Type *type_ = checkType(visitor, type), *def_ = NULL;
    if (typeIs(type_, Error) || def == NULL) {
        node->type = type_;
        return;
    }

    def_ = checkType(visitor, def);
    if (typeIs(def_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (!isTypeAssignableFrom(type_, def_)) {
        logError(ctx->L,
                 &def->loc,
                 "parameter default value type '{t}' is incompatible with "
                 "parameter '{t}'",
                 (FormatArg[]){{.t = def_}, {.t = type_}});
        node->type = ERROR_TYPE(ctx);
    }
    else
        node->type = type_;
}

const Type *checkFunctionSignature(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *params = node->funcDecl.signature->params, *param = params,
            *ret = node->funcDecl.signature->ret, *body = node->funcDecl.body;
    u64 paramsCount = countAstNodes(params);

    const Type **params_ = mallocOrDie(sizeof(Type *) * paramsCount);
    const Type *ret_ = ret ? checkType(visitor, ret) : makeAutoType(ctx->types);

    const Type *type = ret_;
    u64 defaultValues = 0;
    for (u64 i = 0; param; param = param->next, i++) {
        params_[i] = param->type ?: checkType(visitor, param);
        if (typeIs(params_[i], Error))
            type = params_[i];

        defaultValues += (param->funcParam.def != NULL);
    }

    if (typeIs(type, Error)) {
        free(params_);
        return node->type = ERROR_TYPE(ctx);
    }

    if (node->list.first && node->list.first != node) {
        const Type *found = matchOverloadedFunction(ctx,
                                                    node->list.first->type,
                                                    params_,
                                                    paramsCount,
                                                    NULL,
                                                    node->flags & flgConst);
        if (found) {
            // conflict
            logError(
                ctx->L,
                &node->loc,
                "function '{s}' overload with signature {t} already declared",
                (FormatArg[]){{.s = node->funcDecl.name}, {.t = found}});
            logNote(ctx->L,
                    &found->func.decl->loc,
                    "previous declaration found here",
                    NULL);
            free(params_);

            return node->type = ERROR_TYPE(ctx);
        }
    }

    node->type =
        makeFuncType(ctx->types,
                     &(Type){.tag = typFunc,
                             .flags = node->flags & flgConst,
                             .func = {.paramsCount = paramsCount,
                                      .params = params_,
                                      .retType = ret_,
                                      .defaultValuesCount = defaultValues,
                                      .name = getDeclarationName(node),
                                      .decl = node}});
    free(params_);
    return node->type;
}

const Type *checkFunctionBody(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *body = node->funcDecl.body;

    const Type *type = node->type;
    const Type *ret_ = type->func.retType;

    const Type *body_ = checkType(visitor, body);

    if (typeIs(body_, Error))
        return node->type = ERROR_TYPE(ctx);

    if (nodeIs(body, BlockStmt)) {
        AstNode *ret = node->funcDecl.signature->ret;
        // Can be updated by return statement
        body_ = ret ? ret->type : body_;
    }

    if (typeIs(body_, Auto))
        body_ = makeVoidType(ctx->types);

    if (ret_ == body_)
        return type;

    if (!isTypeAssignableFrom(ret_, body_)) {
        logError(ctx->L,
                 &node->loc,
                 "return type '{t}' of function declaration doesn't match type "
                 "'{t}' returned in body.",
                 (FormatArg[]){{.t = ret_}, {.t = body_}});
        return node->type = ERROR_TYPE(ctx);
    }

    if (typeIs(ret_, Auto))
        return node->type = changeFunctionRetType(ctx->types, type, body_);

    return type;
}

static void checkFunctionDecl(AstVisitor *visitor, AstNode *node)
{
    const Type *type = checkFunctionSignature(visitor, node);
    if (!typeIs(type, Error) && node->funcDecl.body)
        checkFunctionBody(visitor, node);
}

static void checkTypeDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *aliased = checkType(visitor, node->typeDecl.aliased);
    if (typeIs(aliased, Error)) {
        node->type = aliased;
        return;
    }

    node->type = makeAliasType(
        ctx->types, aliased, node->typeDecl.name, node->flags & flgConst);
}

static void checkUnionDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *members = node->unionDecl.members, *member = members;
    u64 count = node->tupleType.len;
    const Type **members_ = mallocOrDie(sizeof(Type *) * count);

    for (u64 i = 0; member; member = member->next) {
        members_[i] = checkType(visitor, member);
        if (typeIs(members_[i], Error))
            node->type = ERROR_TYPE(ctx);
    }

    if (typeIs(node->type, Error)) {
        free(members_);
        return;
    }

    node->type = makeUnionType(ctx->types, members_, count);

    free(members_);
}

static void checkGenericDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *param = node->genericDecl.params, *decl = node->genericDecl.decl;
    bool inferrable = false;
    node->type = makeGenericType(ctx->types, node, inferrable);
}

static void checkInterfaceDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *member = node->interfaceDecl.members;
    u64 membersCount = countAstNodes(member);
    StructMember *members = mallocOrDie(sizeof(StructMember) * membersCount);

    for (u64 i = 0; member; member = member->next, i++) {
        const Type *type = checkType(visitor, member);
        if (typeIs(type, Error)) {
            node->type = ERROR_TYPE(ctx);
            continue;
        }
        members[i] = (StructMember){
            .name = getDeclarationName(member), .type = type, .decl = member};
    }

    if (typeIs(node->type, Error))
        goto checkInterfaceMembersError;

    node->type =
        makeInterfaceType(ctx->types,
                          &(Type){.name = getDeclarationName(node),
                                  .tag = typInterface,
                                  .flags = node->flags,
                                  .tInterface = {.members = members,
                                                 .membersCount = membersCount,
                                                 .decl = node}});

checkInterfaceMembersError:
    if (members)
        free(members);
}

static void checkBlockStmt(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *stmts = node->blockStmt.stmts, *stmt = stmts;

    __typeof(ctx->block) block = ctx->block;
    ctx->block.current = NULL;
    ctx->block.self = node;
    for (; stmt; stmt = stmt->next) {
        ctx->block.previous = ctx->block.current;
        ctx->block.current = stmt;
        const Type *type = checkType(visitor, stmt);
        if (typeIs(type, Error)) {
            node->type = ERROR_TYPE(ctx);
            ctx->block = block;
            return;
        }

        if (nodeIs(stmt, ReturnStmt)) {
            if (node->type && !isTypeAssignableFrom(node->type, type)) {
                logError(ctx->L,
                         &stmt->loc,
                         "inconsistent return types within block, got '{t}', "
                         "expecting '{t}'",
                         (FormatArg[]){{.t = type}, {.t = node->type}});
                node->type = ERROR_TYPE(ctx);
                ctx->block = block;
                return;
            }
            node->type = type;
        }
    }
    ctx->block = block;

    if (node->type == NULL)
        node->type = makeAutoType(ctx->types);
}

static void checkReturnStmt(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->returnStmt.expr, *func = node->returnStmt.func,
            *ret = func->funcDecl.signature->ret;
    const Type *expr_ =
        expr ? checkType(visitor, expr) : makeVoidType(ctx->types);

    if (typeIs(expr_, Error)) {
        node->type = expr_;
        return;
    }

    if (ret) {
        if (!isTypeAssignableFrom(ret->type, expr_)) {
            logError(ctx->L,
                     &node->loc,
                     "inconsistent return type, expecting '{t}', got '{t}'",
                     (FormatArg[]){{.t = ret->type}, {.t = expr_}});
            logNote(ctx->L,
                    &ret->loc,
                    "return type first declared or deduced here",
                    NULL);

            node->type = ERROR_TYPE(ctx);
        }
        else
            node->type = ret->type;
    }
    else {
        node->type = expr_;
        func->funcDecl.signature->ret =
            makeTypeReferenceNode(ctx->pool, expr_, &node->loc);
    }
}

static void checkDeferStmt(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *type = checkType(visitor, node->deferStmt.expr);

    if (typeIs(type, Error)) {
        node->type = type;
        return;
    }

    AstNode *block = node->deferStmt.block;
    insertAstNode(&block->blockStmt.epilogue, node->exprStmt.expr);
    node->tag = astNop;
    clearAstBody(node);

    node->type = makeVoidType(ctx->types);
}

static void checkBreakOrContinueStmt(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    node->type = makeVoidType(ctx->types);
}

static void checkIfStmt(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *cond = node->ifStmt.cond, *then = node->ifStmt.body,
            *otherwise = node->ifStmt.otherwise;

    const Type *cond_ = checkType(visitor, cond);
    if (typeIs(cond_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (!isTruthyType(cond_)) {
        logError(
            ctx->L,
            &cond->loc,
            "expecting a truthy type in an if statement condition, got '{t}'",
            (FormatArg[]){{.t = cond_}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    const Type *then_ = checkType(visitor, then);
    if (typeIs(then_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (otherwise == NULL) {
        node->type = then_;
        return;
    }

    const Type *otherwise_ = checkType(visitor, otherwise);
    if (typeIs(otherwise_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (typeIs(then_, Auto) || typeIs(otherwise_, Auto)) {
        node->type = typeIs(then_, Auto) ? otherwise_ : then_;
        return;
    }

    if (!isTypeAssignableFrom(then_, otherwise_)) {
        logError(ctx->L,
                 &otherwise->loc,
                 "inconsistent return type on if statement branches, then type "
                 "'{t}' is not assignable to else type '{t}'",
                 (FormatArg[]){{.t = then_}, {.t = otherwise_}});
        node->type = ERROR_TYPE(ctx);
        return;
    }
    node->type = then_;
}

static void checkWhileStmt(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *cond = node->whileStmt.cond, *body = node->whileStmt.body;

    const Type *cond_ = checkType(visitor, cond);
    if (typeIs(cond_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (!isTruthyType(cond_)) {
        logError(ctx->L,
                 &cond->loc,
                 "expecting a truthy type in an `while` statement condition, "
                 "got '{t}'",
                 (FormatArg[]){{.t = cond_}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    const Type *body_ = checkType(visitor, body);
    if (typeIs(body_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    node->type = body_;
}

static void checkCallExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);

    AstNode *currentCall = ctx->currentCall;
    ctx->currentCall = node;
    AstNode *callee = node->callExpr.callee;
    const Type *callee_ = checkType(visitor, callee);
    ctx->currentCall = currentCall;

    if (typeIs(callee_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (typeIs(callee_, Func)) {
        checkFunctionCallEpilogue(visitor, node);
    }
}

static void checkTupleExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *elements = node->tupleExpr.elements, *element = elements;
    node->tupleExpr.len = node->tupleExpr.len ?: countAstNodes(elements);
    const Type **elements_ = mallocOrDie(sizeof(Type *) * node->tupleExpr.len);

    for (u64 i = 0; element; element = element->next, i++) {
        elements_[i] = checkType(visitor, element);
        if (typeIs(elements_[i], Error))
            node->type = element->type;
    }

    if (!typeIs(node->type, Error)) {
        node->type = makeTupleType(
            ctx->types, elements_, node->tupleExpr.len, node->flags & flgConst);
    }

    free(elements_);
}

static void checkMemberExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *target = node->memberExpr.target,
            *member = node->memberExpr.member;
    const Type *target_ = checkType(visitor, target);
    if (typeIs(target_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    csAssert0(nodeIs(member, Path));
    node->type =
        checkPathElement(visitor, stripAll(target_), member->path.elements);
}

static void checkSpreadExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->spreadExpr.expr;
    if (nodeIs(expr, TupleExpr)) {
        astVisitManyNodes(visitor, expr->tupleExpr.elements);
        getLastAstNode(expr->tupleExpr.elements)->next = node->next;
        *node = *expr->tupleExpr.elements;
        return;
    }

    const Type *type = checkType(visitor, expr);

    if (!typeIs(type, Tuple)) {
        logError(
            ctx->L,
            &node->loc,
            "spread operator `...` can only be used with tuple expressions",
            NULL);
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (type->tuple.count == 0) {
        node->tag = astNop;
        return;
    }

    AstNode *variable = makeSpreadVariable(ctx, expr), *parts = NULL,
            *it = NULL;
    for (u64 i = 0; i < type->tuple.count; i++) {
        const Type *type_ = type->tuple.members[i];
        AstNode *tmp = makeAstNode(
            ctx->pool,
            &node->loc,
            &(AstNode){
                .tag = astMemberExpr,
                .flags = type->flags | type_->flags,
                .type = type_,
                .memberExpr = {
                    .target =
                        makePathFromIdent(ctx->pool, variable->varDecl.names),
                    .member = makeAstNode(
                        ctx->pool,
                        &node->loc,
                        &(AstNode){.tag = astIntegerLit,
                                   .type = getPrimitiveType(ctx->types, prtI64),
                                   .intLiteral.value = (i16)i})}});
        if (parts == NULL)
            parts = it = tmp;
        else
            it = it->next = tmp;
    }

    it->next = node->next;
    *node = *parts;
    addBlockLevelDeclaration(ctx, variable);
}

static void checkProgram(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *decl = node->program.decls;
    astVisit(visitor, node->program.module);
    astVisitManyNodes(visitor, node->program.top);

    for (; decl; decl = decl->next) {
        ctx->root.previous = ctx->root.current;
        ctx->root.current = decl;
        astVisit(visitor, decl);
    }
}

static void withSavedStack(Visitor func, AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    __typeof(ctx->stack) stack = ctx->stack;

    func(visitor, node);

    ctx->stack = stack;
}

AstNode *checkAst(CompilerDriver *driver, AstNode *node)
{
    TypingContext context = {.L = driver->L,
                             .pool = &driver->pool,
                             .strings = &driver->strPool,
                             .types = driver->typeTable};

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context, {
        [astProgram] = checkProgram,
        [astTypeRef] = checkTypeRef,
        [astNullLit] = checkLiteral,
        [astBoolLit] = checkLiteral,
        [astCharLit] = checkLiteral,
        [astIntegerLit] = checkLiteral,
        [astFloatLit] = checkLiteral,
        [astStringLit] = checkLiteral,
        [astPrimitiveType] = checkBuiltinType,
        [astStringType] = checkBuiltinType,
        [astAutoType] = checkBuiltinType,
        [astVoidType] = checkBuiltinType,
        [astPointerType] = checkPointerType,
        [astTupleType] = checkTupleType,
        [astDefine] = checkDefine,
        [astIdentifier] = checkIdentifier,
        [astPath] = checkPath,
        [astFuncDecl] = checkFunctionDecl,
        [astFuncParam] = checkFunctionParam,
        [astVarDecl] = checkVarDecl,
        [astTypeDecl] = checkTypeDecl,
        [astUnionDecl] = checkUnionDecl,
        [astGenericDecl] = checkGenericDecl,
        [astStructField] = checkStructField,
        [astStructDecl] = checkStructDecl,
        [astInterfaceDecl] = checkInterfaceDecl,
        [astReturnStmt] = checkReturnStmt,
        [astBlockStmt] = checkBlockStmt,
        [astDeferStmt] = checkDeferStmt,
        [astBreakStmt] = checkBreakOrContinueStmt,
        [astContinueStmt] = checkBreakOrContinueStmt,
        [astIfStmt] = checkIfStmt,
        [astWhileStmt] = checkWhileStmt,
        [astCallExpr] = checkCallExpr,
        [astTupleExpr] = checkTupleExpr,
        [astMemberExpr] = checkMemberExpr,
        [astSpreadExpr] = checkSpreadExpr,
        [astBinaryExpr] = checkBinaryExpr

    }, .fallback = astVisitFallbackVisitAll, .dispatch = withSavedStack);
    // clang-format on

    astVisit(&visitor, node);

    return node;
}