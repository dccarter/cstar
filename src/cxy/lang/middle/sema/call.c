//
// Created by Carter Mbotho on 2024-01-09.
//

#include "check.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"

#include "core/alloc.h"

static inline u64 getCalleeContextFlags(const AstNode *node)
{
    if (nodeIs(node, Path) && node->path.elements->next) {
        return node->path.elements->flags & flgConst;
    }
    else if (nodeIs(node, MemberExpr)) {
        return node->memberExpr.target->flags & flgConst;
    }
    return flgNone;
}

static inline bool isSuperPath(const AstNode *node)
{
    if (!nodeIs(node, Path) || node->path.elements->next != NULL)
        return false;
    node = node->path.elements;
    return node->pathElement.isKeyword && node->pathElement.name == S_super;
}

static void reAssignCalleeType(AstNode *node, const Type *type)
{
    if (nodeIs(node, Path)) {
        AstNode *elem = getLastAstNode(node->path.elements);
        AstNode *resolved = elem->pathElement.resolvesTo;
        elem->type = type;
        if (!nodeIs(resolved, FuncParamDecl) && !nodeIs(resolved, FieldDecl))
            elem->pathElement.resolvesTo = type->func.decl;
    }
    else if (nodeIs(node, MemberExpr)) {
        AstNode *member = node->memberExpr.member;
        member->type = type;
        if (nodeIs(member, Identifier)) {
            AstNode *resolved = member->ident.resolvesTo;
            if (!nodeIs(resolved, FuncParamDecl) &&
                !nodeIs(resolved, FieldDecl))
                member->ident.resolvesTo = type->func.decl;
        }
    }
    else if (nodeIs(node, Identifier)) {
        AstNode *resolved = node->ident.resolvesTo;
        if (!nodeIs(resolved, FuncParamDecl) && !nodeIs(resolved, FieldDecl))
            node->ident.resolvesTo = type->func.decl;
    }
    node->type = type;
}

static void checkFunctionCallEpilogue(AstVisitor *visitor,
                                      const Type *func,
                                      AstNode *node,
                                      u64 flags)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *callee = node->callExpr.callee, *args = node->callExpr.args;
    const Type *callee_ = NULL;

    AstNode *arg = args, *prev = NULL;
    for (u64 i = 0; arg; arg = arg->next, i++) {
        const Type *type = arg->type ?: checkType(visitor, arg);
        if (typeIs(type, Error)) {
            node->type = ERROR_TYPE(ctx);
            prev = arg;
            continue;
        }

        if (hasFlag(type, Closure)) {
            arg = transformClosureArgument(visitor, arg);
            if (prev == NULL)
                node->callExpr.args = arg;
            else
                prev->next = arg;
            prev = arg;

            if (typeIs(arg->type, Error)) {
                node->type = ERROR_TYPE(ctx);
                continue;
            }
        }
        prev = arg;
    }
    if (typeIs(node->type, Error))
        return;

    if (nodeIs(node->callExpr.args, Noop))
        node->callExpr.args = args->next;
    args = node->callExpr.args;
    u64 argsCount = countAstNodes(args);
    const Type **argTypes = mallocOrDie(sizeof(Type *) * argsCount);
    arg = args;
    for (u64 i = 0; arg; arg = arg->next, i++) {
        argTypes[i] = arg->type;
    }

    callee_ = matchOverloadedFunction(
        ctx->L, func, argTypes, argsCount, &node->loc, flags);
    free(argTypes);

    if (callee_ == NULL || typeIs(callee_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    reAssignCalleeType(callee, callee_);
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
                (FormatArg[]){{.s = callee_->name}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    u64 paramsCount = callee_->func.paramsCount;
    arg = node->callExpr.args;
    argsCount = countAstNodes(arg);

    if (paramsCount > argsCount) {
        // Add default parameters to function call
        AstNode *decl = callee_->func.decl;
        AstNode *params = nodeIs(decl, FuncDecl)
                              ? decl->funcDecl.signature->params
                              : decl->funcType.params;
        if (params->funcParam.name == S_this)
            params = params->next;

        AstNode *param = getNodeAtIndex(params, argsCount);
        csAssert0(param);

        if (node->callExpr.args == NULL) {
            node->callExpr.args =
                deepCloneAstNode(ctx->pool, param->funcParam.def);
            arg = node->callExpr.args;
            param = param->next;
        }
        else {
            arg = getLastAstNode(node->callExpr.args);
        }

        for (; param; param = param->next) {
            arg->next = deepCloneAstNode(ctx->pool, param->funcParam.def);
            arg = arg->next;
        }
    }
}

void checkCallExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);

    AstNode *currentCall = ctx->currentCall;
    ctx->currentCall = node;
    AstNode *callee = node->callExpr.callee;
    u64 flags = getCalleeContextFlags(callee);

    const Type *callee_ = checkType(visitor, callee);

    ctx->currentCall = currentCall;

    if (callee_ == NULL || typeIs(callee_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    callee_ = flattenWrappedType(callee_, &flags);
    const Type *rawType = stripPointerOrReferenceOnce(callee_, NULL);
    if (isClassOrStructType(rawType)) {
        AstNode *symbol = nodeIs(callee, Path)      ? resolveAstNode(callee)
                          : nodeIs(callee, TypeRef) ? getTypeDecl(rawType)
                                                    : NULL;

        if (isClassOrStructAstNode(symbol)) {
            if (hasFlag(symbol, Abstract)) {
                logError(ctx->L,
                         &callee->loc,
                         "cannot create an instance of '{t}' because it is an "
                         "abstract type",
                         (FormatArg[]){{.t = rawType}});
                node->type = ERROR_TYPE(ctx);
                return;
            }
            node->type = transformToConstructCallExpr(visitor, node);
            return;
        }

        const Type *overload = NULL;
        if (isSuperPath(callee)) {
            overload = findStructMemberType(rawType, S_InitOverload);
        }
        else {
            flags &= ~flgConst;
            overload = findStructMemberType(rawType, S_CallOverload);
        }

        if (overload) {
            callee_ = overload;
            callee = makeMemberExpr(
                ctx->pool,
                &callee->loc,
                callee->flags,
                callee,
                makeIdentifier(
                    ctx->pool, &callee->loc, overload->name, 0, NULL, overload),
                NULL,
                overload);
            node->callExpr.callee = callee;
        }
    }
    else if (typeIs(rawType, Tuple) && hasFlag(rawType, FuncTypeParam)) {
        node->callExpr.callee = makeMemberExpr(
            ctx->pool,
            &callee->loc,
            callee->flags,
            deepCloneAstNode(ctx->pool, callee),
            makeIntegerLiteral(ctx->pool,
                               &callee->loc,
                               1,
                               NULL,
                               getPrimitiveType(ctx->types, prtI64)),
            NULL,
            rawType->tuple.members[1]);

        node->callExpr.args = makeMemberExpr(
            ctx->pool,
            &callee->loc,
            callee->flags,
            callee,
            makeIntegerLiteral(ctx->pool,
                               &callee->loc,
                               0,
                               NULL,
                               getPrimitiveType(ctx->types, prtI64)),
            node->callExpr.args,
            rawType->tuple.members[0]);

        node->type = checkType(visitor, node);
        return;
    }

    if (!typeIs(callee_, Func)) {
        logError(ctx->L,
                 &callee->loc,
                 "type '{t}' is not a callable type",
                 (FormatArg[]){{.t = callee_}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    checkFunctionCallEpilogue(visitor, callee_, node, flags);
    if (typeIs(node->type, Error))
        return;

    callee_ = node->callExpr.callee->type;
    AstNode *arg = node->callExpr.args, *prev = NULL;
    bool isVariadic = hasFlag(callee_, Variadic);
    for (u64 i = 0; arg; arg = arg->next, i++) {
        if (isVariadic && i >= callee_->func.paramsCount)
            break;

        const Type *type = callee_->func.params[i], *expr = arg->type;
        if (hasFlag(type, FuncTypeParam))
            arg->type = type;
        if (!evalExplicitConstruction(visitor, type, arg)) {
            logError(ctx->L,
                     &arg->loc,
                     "incompatible argument types, expecting '{t}' but got "
                     "'{t}'",
                     (FormatArg[]){{.t = type}, {.t = type}});
            prev = arg;
            continue;
        }

        AstNode *newArg = transformToUnionValue(ctx, arg, type, expr);
        if (newArg != arg) {
            newArg->next = arg->next;
            if (prev)
                prev->next = newArg;
            else
                node->callExpr.args = newArg;
            arg = newArg;
        }
        prev = arg;

        if (isSliceType(type) && isArrayType(expr)) {
            transformArrayExprToSlice(visitor, type, arg);
        }

        if (!hasFlag(type, Optional) || hasFlag(expr, Optional))
            continue;

        type = getOptionalTargetType(type);
        if (nodeIs(arg, NullLit)) {
            if (!transformOptionalNone(visitor, arg, type))
                node->type = ERROR_TYPE(ctx);
        }
        else {
            arg->type = type;
            if (!transformOptionalSome(
                    visitor, arg, copyAstNode(ctx->pool, arg)))
                node->type = ERROR_TYPE(ctx);
        }

        if (typeIs(node->type, Error))
            return;
    }
}
