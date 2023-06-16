//
// Created by Carter on 2023-04-18.
//

#include "lang/codegen.h"
#include "lang/semantics.h"

#include "lang/ttable.h"

#include "core/alloc.h"

#include <memory.h>

// static bool isBaseFunction(const Type *target, const AstNode *node)
//{
//     if ()
// }

static const Type *wrapFuncArgInClosure(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *orig = copyAstNode(ctx->pool, node);
    const Type *type = node->type;
    AstNode *params = copyAstNode(ctx->pool, type->func.decl->funcDecl.params);
    AstNode *param = params;
    AstNode *args = NULL, *arg = NULL;
    for (; param; param = param->next) {
        AstNode *newArg =
            makeAstNode(ctx->pool,
                        &node->loc,
                        &(AstNode){.tag = astIdentifier,
                                   .type = param->type,
                                   .ident.value = param->funcParam.name});
        if (arg == NULL) {
            arg = newArg;
            args = newArg;
        }
        else
            arg->next = newArg;
    }

    memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);
    node->tag = astClosureExpr;
    node->closureExpr.params = params;
    node->closureExpr.body =
        makeAstNode(ctx->pool,
                    &node->loc,
                    &(AstNode){.tag = astCallExpr,
                               .type = type,
                               .callExpr = {.callee = orig, .args = args}});

    return evalType(visitor, node);
}

static const Type *functionTypeParamToCall(SemanticsContext *ctx,
                                           const Type *type,
                                           AstNode *node)
{
    // b._1
    AstNode *newCallee = makeAstNode(
        ctx->pool,
        &node->callExpr.callee->loc,
        &(AstNode){.tag = astMemberExpr,
                   .type = type->tuple.members[1],
                   .flags = node->callExpr.callee->flags,
                   .memberExpr = {.target = node->callExpr.callee,
                                  .member = makeAstNode(
                                      ctx->pool,
                                      &node->callExpr.callee->loc,
                                      &(AstNode){.tag = astIntegerLit,
                                                 .intLiteral.value = 1})}});
    // b._0
    AstNode *arg = makeAstNode(
        ctx->pool,
        &node->callExpr.callee->loc,
        &(AstNode){.tag = astMemberExpr,
                   .next = node->callExpr.args,
                   .type = type->tuple.members[0],
                   .flags = node->callExpr.callee->flags,
                   .memberExpr = {.target = node->callExpr.callee,
                                  .member = makeAstNode(
                                      ctx->pool,
                                      &node->callExpr.callee->loc,
                                      &(AstNode){.tag = astIntegerLit,
                                                 .intLiteral.value = 0})}});

    node->callExpr.callee = newCallee;
    node->callExpr.args = arg;

    return type->tuple.members[1];
}

static const Type *structCallToFunctionCall(AstVisitor *visitor,
                                            const Type *type,
                                            AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *raw = stripPointer(type);
    AstNode *callee = node->callExpr.callee;

    AstNode *symbol = findSymbolOnly(raw->tStruct.env, "op_call");
    if (!symbol) {
        logError(ctx->L,
                 &callee->loc,
                 "call expression target of type '{t}' does not have a call "
                 "operator `()` overload",
                 (FormatArg[]){{.t = raw}});
        return NULL;
    }

    // transform x(...) => x.op_call(...)
    node->callExpr.callee = makeAstNode(
        ctx->pool,
        &callee->loc,
        &(AstNode){.tag = astMemberExpr,
                   .flags = callee->flags,
                   .type = symbol->type,
                   .memberExpr = {.target = callee,
                                  .member = makeAstNode(
                                      ctx->pool,
                                      &callee->loc,
                                      &(AstNode){.tag = astIdentifier,
                                                 .flags = callee->flags,
                                                 .type = symbol->type,
                                                 .ident.value = "op_call"})}});

    return symbol->type;
}

static void generateClosureCapture(CodegenContext *ctx,
                                   const Type *type,
                                   cstring name,
                                   u64 index)
{
    generateTypeUsage(ctx, stripPointer(type->func.params[0]));

    format(ctx->state,
           " {s}{u64} = {{",
           (FormatArg[]){{.s = name}, {.u64 = index}});
    for (u64 i = 0; i < type->func.capturedNamesCount; i++) {
        const Type *captureType = stripPointer(type->func.params[0]);
        if (i != 0)
            format(ctx->state, ", ", NULL);
        if (captureType->tuple.members[i]->flags & flgCapturePointer)
            format(
                ctx->state,
                "._{u64} = &{s}",
                (FormatArg[]){{.u64 = i}, {.s = type->func.captureNames[i]}});
        else
            format(
                ctx->state,
                "._{u64} = {s}",
                (FormatArg[]){{.u64 = i}, {.s = type->func.captureNames[i]}});
    }
    format(ctx->state, "}; ", NULL);
}

static const Type *evalOverloadFunctionType(AstVisitor *visitor,
                                            AstNode *node,
                                            u64 count)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *callee = node->callExpr.callee;
    AstNode *arg = node->callExpr.args;
    u32 overload = node->callExpr.overload;

    SymbolRef *symbol = findSymbolRefByNode(ctx, ctx->env, callee, false);

    evalType(visitor, callee);

    if (symbol == NULL || !nodeIs(symbol->node, FuncDecl))
        return callee->type;

    const Type **params = callocOrDie(count, sizeof(Type *));
    for (u64 i = 0; arg; arg = arg->next, i++)
        params[i] = arg->type ?: evalType(visitor, arg);

    u64 flags = nodeIs(callee, Path) &&
                        typeIs(stripAll(callee->path.elements->type), Struct)
                    ? (callee->path.elements->flags & flgConst)
                    : flgNone;

    AstNode *decl =
        overload ? getSymbolRefAt(symbol, overload - 1)->node
                 : symbolRefLookupFuncDeclBySignature(
                       ctx, symbol, flags, params, count, &callee->loc, true);

    free(params);

    if (decl) {
        getLastAstNode(callee)->type = decl->type;
        callee->type = decl->type;
    }

    return callee->type;
}

static inline void checkCallWithStack(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *arg = node->callExpr.args;
    u64 count = countAstNodes(arg);

    ctx->currentCall = node;
    node->callExpr.callee->parentScope = node;
    const Type *callee = evalOverloadFunctionType(visitor, node, count);
    if (callee == NULL || typeIs(callee, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    bool paramsEvaluated = false;
    const Type *calleeRaw = stripPointer(callee);
    if (typeIs(calleeRaw, Struct)) {
        if (nodeIs(node->callExpr.callee, Path)) {
            AstNode *decl =
                findSymbolOnlyByNode(ctx->env, node->callExpr.callee);
            if (nodeIs(decl, StructDecl)) {
                evalConstructorCall(visitor,
                                    callee,
                                    node,
                                    node->callExpr.callee,
                                    node->callExpr.args);
                return;
            }
        }

        callee = structCallToFunctionCall(visitor, callee, node);
        if (callee == NULL || typeIs(callee, Error)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }
        paramsEvaluated = true;
    }

    bool isNativeCallee = hasFlag(callee, Native);
    if (hasFlag(callee, FuncTypeParam) && !isNativeCallee) {
        callee = functionTypeParamToCall(ctx, callee, node);
    }

    callee = unwrapType(callee, NULL);
    if (callee->tag != typFunc) {
        logError(ctx->L,
                 &node->callExpr.callee->loc,
                 "expression of type '{t}' cannot be invoked, expecting a "
                 "function",
                 (FormatArg[]){{.t = callee}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    node->type = callee->func.retType;
    u64 paramsCount = callee->func.paramsCount, i = 0;
    if (callee->flags & (flgClosure | flgFuncTypeParam)) {
        paramsCount--;
        i = 1;
    }

    u64 withoutDefaulted = paramsCount - callee->func.defaultValuesCount;
    count = countAstNodes(arg);
    if (count < withoutDefaulted) {
        logError(ctx->L,
                 &node->loc,
                 "few arguments provided to function of type "
                 "'{t}', expecting at least '{u64}' but got '{u64}'",
                 (FormatArg[]){
                     {.t = callee}, {.u64 = withoutDefaulted}, {.u64 = count}});
        return;
    }

    if (count > paramsCount) {
        logError(
            ctx->L,
            &node->loc,
            "too many arguments provided to function of type "
            "'{t}', at most '{u64}' but got '{u64}'",
            (FormatArg[]){{.t = callee}, {.u64 = paramsCount}, {.u64 = count}});
        return;
    }

    for (; arg; arg = arg->next, i++) {
        const Type *type = arg->type ?: evalType(visitor, arg);
        const Type *expected = callee->func.params[i];
        if (hasFlag(expected, FuncTypeParam) && !isNativeCallee) {
            if (!hasFlags(type, flgClosure | flgFuncTypeParam))
                type = wrapFuncArgInClosure(visitor, arg);

            if (!hasFlag(type, FuncTypeParam))
                expected = expected->tuple.members[1];
        }

        if (stripPointer(expected)->tag == typThis)
            expected = makePointerType(
                ctx->typeTable,
                callee->func.decl->parentScope->type,
                callee->func.decl->parentScope->type->flags | expected->flags);

        if (!evalExplicitConstruction(visitor, expected, arg)) {
            logError(ctx->L,
                     &arg->loc,
                     "incompatible argument types, expecting '{t}' but got "
                     "'{t}'",
                     (FormatArg[]){{.t = expected}, {.t = type}});
        }
    }

    if (paramsCount > count) {
        // Add default parameters to function call
        AstNode *param =
            getNodeAtIndex(callee->func.decl->funcDecl.params, count);
        csAssert0(param);

        if (node->callExpr.args == NULL) {
            node->callExpr.args = copyAstNode(ctx->pool, param->funcParam.def);
            arg = node->callExpr.args;
            param = param->next;
        }
        else {
            arg = getLastAstNode(node->callExpr.args);
        }

        for (; param; param = param->next) {
            arg->next = copyAstNode(ctx->pool, param->funcParam.def);
            arg = arg->next;
        }
    }
}

void generateCallExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);

    const Type *type = resolveType(node->callExpr.callee->type);
    const AstNode *parent =
        type->func.decl ? type->func.decl->parentScope : NULL;
    u32 index = type->func.decl ? type->func.decl->funcDecl.index : 0;

    const char *name =
        hasFlags(type, flgClosureStyle | flgClosure)
            ? makeAnonymousVariable(ctx->strPool, "_closure_capture")
            : "";

    if (hasFlags(type, flgClosureStyle | flgClosure))
        format(ctx->state, "({{{>}\n", NULL);
    if (hasFlag(type, ClosureStyle)) {
        const AstNode *arg = node->callExpr.args;
        for (u64 i = 1; arg; arg = arg->next, i++) {
            if (hasFlag(arg->type, Closure)) {
                generateClosureCapture(ctx, arg->type, name, i);
            }
        }
    }
    else if (hasFlag(type, Closure)) {
        generateClosureCapture(ctx, type, name, 0);
    }

    astConstVisit(visitor, node->callExpr.callee);
    
    if (index)
        format(ctx->state, "{u32}", (FormatArg[]){{.u32 = index}});

    bool isMember = nodeIs(parent, StructDecl);
    if (hasFlag(type, Closure)) {
        format(ctx->state, "(&{s}0", (FormatArg[]){{.s = name}});
        if (type->func.paramsCount > 1)
            format(ctx->state, ", ", NULL);
    }
    else if (isMember) {
        const AstNode *callee = node->callExpr.callee;
        bool needsThis =
            nodeIs(callee, Identifier) ||
            (!nodeIs(callee, MemberExpr) &&
             (nodeIs(callee, Path) && callee->path.elements->next == NULL));

        if (needsThis) {
            if (hasFlag(node->callExpr.callee, AddSuper))
                format(ctx->state, "(&(this->super)", NULL);
            else
                format(ctx->state, "(this", NULL);
        }
        else {
            format(ctx->state, "(", NULL);
            const AstNode *target, *elem;
            if (nodeIs(callee, Path)) {
                target = callee->path.elements, elem = callee->path.elements;
                while (true) {
                    if (target->next == NULL || target->next->next == NULL)
                        break;
                    target = target->next;
                }

                if (!typeIs(target->type, Pointer))
                    format(ctx->state, "&", NULL);

                for (;; elem = elem->next) {
                    astConstVisit(visitor, elem);
                    if (elem == target)
                        break;
                    if (typeIs(elem->type, Pointer))
                        format(ctx->state, "->", NULL);
                    else
                        format(ctx->state, ".", NULL);
                }
            }
            else {
                target = callee->memberExpr.target;
                if (!typeIs(target->type, Pointer))
                    format(ctx->state, "&", NULL);
                astConstVisit(visitor, target);
            }
        }
    }
    else {
        format(ctx->state, "(", NULL);
    }
    {
        const AstNode *arg = node->callExpr.args;
        for (u64 i = 0; arg; arg = arg->next, i++) {
            const Type *param = type->func.params[i];
            if (isMember || i != 0)
                format(ctx->state, ", ", NULL);

            if (hasFlag(arg->type, Closure)) {
                format(ctx->state, "(", NULL);
                generateTypeUsage(ctx, param);
                format(ctx->state,
                       "){{._0 = &{s}{u64}, ._1 = ",
                       (FormatArg[]){{.s = name}, {.u64 = i + 1}});
                astConstVisit(visitor, arg->type->func.decl);
                format(ctx->state, "_fwd}", NULL);
            }
            else if (isSliceType(param) && !isSliceType(arg->type)) {
                generateArrayToSlice(visitor, param, arg);
            }
            else {
                astConstVisit(visitor, arg);
            }
        }
    }

    if (hasFlags(type, flgClosureStyle | flgClosure))
        format(ctx->state, ");{<}\n})", NULL);
    else
        format(ctx->state, ")", NULL);
}

void checkCall(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    __typeof(ctx->stack) stack = ctx->stack;
    ctx->currentCall = node;
    checkCallWithStack(visitor, node);
    ctx->stack = stack;
}