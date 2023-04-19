//
// Created by Carter on 2023-04-18.
//

#include "lang/codegen.h"
#include "lang/semantics.h"

#include "lang/ttable.h"

#include <memory.h>

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

static const Type *structCallToFunctionCall(SemanticsContext *ctx,
                                            const Type *type,
                                            AstNode *node)
{
    const Type *raw = stripPointer(type);
    SymbolRef *sym = findSymbolRef(raw->tStruct.env, "op_call");
    AstNode *callee = node->callExpr.callee;

    if (!sym) {
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
                   .type = sym->node->type,
                   .memberExpr = {.target = callee,
                                  .member = makeAstNode(
                                      ctx->pool,
                                      &callee->loc,
                                      &(AstNode){.tag = astIdentifier,
                                                 .flags = callee->flags,
                                                 .type = sym->node->type,
                                                 .ident.value = "op_call"})}});

    return sym->node->type;
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

void generateCallExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);

    const Type *type = node->callExpr.callee->type;
    const AstNode *parent =
        type->func.decl ? type->func.decl->parentScope : NULL;

    const char *name =
        (type->flags & (flgClosureStyle | flgClosure))
            ? makeAnonymousVariable(ctx->strPool, "_closure_capture")
            : "";

    if (type->flags & (flgClosureStyle | flgClosure))
        format(ctx->state, "({{{>}\n", NULL);
    if (type->flags & flgClosureStyle) {
        const AstNode *arg = node->callExpr.args;
        for (u64 i = 1; arg; arg = arg->next, i++) {
            if (arg->type->flags & flgClosure) {
                generateClosureCapture(ctx, arg->type, name, i);
            }
        }
    }
    else if (type->flags & flgClosure) {
        generateClosureCapture(ctx, type, name, 0);
    }

    astConstVisit(visitor, node->callExpr.callee);
    bool isMember = parent && parent->tag == astStructDecl;
    if (type->flags & flgClosure) {
        format(ctx->state, "(&{s}0", (FormatArg[]){{.s = name}});
        if (type->func.paramsCount > 1)
            format(ctx->state, ", ", NULL);
    }
    else if (isMember) {
        const AstNode *callee = node->callExpr.callee;
        bool needsThis =
            (callee->tag == astIdentifier) ||
            ((callee->tag != astMemberExpr) &&
             (callee->tag == astPath && callee->path.elements->next == NULL));

        if (needsThis) {
            format(ctx->state, "(this", NULL);
        }
        else {
            format(ctx->state, "(", NULL);
            const AstNode *target, *elem;
            if (callee->tag == astPath) {
                target = callee->path.elements, elem = callee->path.elements;
                while (true) {
                    if (target->next == NULL || target->next->next == NULL)
                        break;
                    target = target->next;
                }

                if (target->type->tag != typPointer)
                    format(ctx->state, "&", NULL);

                for (;; elem = elem->next) {
                    astConstVisit(visitor, elem);
                    if (elem == target)
                        break;
                    if (elem->type->tag == typPointer)
                        format(ctx->state, "->", NULL);
                    else
                        format(ctx->state, ".", NULL);
                }
            }
            else {
                target = callee->memberExpr.target;
                if (target->type->tag != typPointer)
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

            if (arg->type->flags & flgClosure) {
                format(ctx->state, "(", NULL);
                generateTypeUsage(ctx, param);
                format(ctx->state,
                       "){{._0 = &{s}{u64}, ._1 = ",
                       (FormatArg[]){{.s = name}, {.u64 = i + 1}});
                astConstVisit(visitor, arg->type->func.decl);
                format(ctx->state, "_fwd}", NULL);
            }
            else {
                astConstVisit(visitor, arg);
            }
        }
    }

    if (type->flags & (flgClosureStyle | flgClosure))
        format(ctx->state, ");{<}\n})", NULL);
    else
        format(ctx->state, ")", NULL);
}

void checkCall(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *callee = evalType(visitor, node->callExpr.callee);
    const Type *calleeRaw = stripPointer(callee);
    AstNode *arg = node->callExpr.args;
    bool paramsEvaluated = false;

    if (typeIs(calleeRaw, Struct)) {
        callee = structCallToFunctionCall(ctx, callee, node);
        if (callee == NULL) {
            node->type = ERROR_TYPE(ctx);
            return;
        }
        paramsEvaluated = false;
    }

    if (callee->flags & flgFuncTypeParam) {
        callee = functionTypeParamToCall(ctx, callee, node);
    }

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

    u64 count = countAstNodes(arg),
        withoutDefaulted = paramsCount - callee->func.defaultValuesCount;
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
        const Type *type = evalType(visitor, arg);
        const Type *expected = callee->func.params[i];
        if (expected->flags & flgFuncTypeParam) {
            if (!hasFlags(type, flgClosure | flgFuncTypeParam))
                type = wrapFuncArgInClosure(visitor, arg);

            if (!hasFlag(type, FuncTypeParam))
                expected = expected->tuple.members[1];
        }

        if (stripPointer(expected)->tag == typThis)
            expected =
                makePointerType(ctx->typeTable,
                                callee->func.decl->parentScope->type,
                                callee->func.decl->parentScope->type->flags);

        if (!isTypeAssignableFrom(expected, type)) {
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
            arg = node->callExpr.args;
        }

        for (; param; param = param->next) {
            arg->next = copyAstNode(ctx->pool, param->funcParam.def);
            arg = arg->next;
        }
    }
}
