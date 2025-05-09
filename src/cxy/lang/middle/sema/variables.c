//
// Created by Carter Mbotho on 2024-01-09.
//

#include "check.h"

#include "../eval/eval.h"

#include "lang/frontend/flag.h"

void checkVarDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *type = node->varDecl.type, *init = node->varDecl.init,
            *name = node->varDecl.names;
    if (hasFlag(name, Comptime) && !evaluate(ctx->evaluator, name)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    const Type *type_ =
        type ? checkType(visitor, type) : makeAutoType(ctx->types);
    const Type *init_ = checkType(visitor, init);
    if (typeIs(init_, Void)) {
        logError(ctx->L,
                 &init->loc,
                 "variable initializer evaluates to `{t}` type",
                 (FormatArg[]){{.t = init_}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (isSliceType(type_) && nodeIs(init, ArrayExpr)) {
        AstNode *newVar = transformArrayExprToSliceCall(ctx, type_, init);
        init_ = checkType(visitor, newVar);
        if (typeIs(init_, Error)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }

        astModifierAdd(&ctx->blockModifier, newVar);
        node->varDecl.init = makeSliceConstructor(ctx, type_, newVar);
        type_ = checkType(visitor, node);
        if (typeIs(type_, Error))
            node->type = ERROR_TYPE(ctx);
        return;
    }

    if (typeIs(type_, Error) || typeIs(init_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (init_ == NULL) {
        node->type = type_;
        return;
    }

    if (isArrayType(type_) && nodeIs(init, ArrayExpr)) {
        const Type *target = resolveAndUnThisType(init_->array.elementType),
                   *initTarget = resolveAndUnThisType(init_->array.elementType);
        if (!typeIs(initTarget, Auto) &&
            !isTypeAssignableFrom(target, initTarget)) {
            logError(ctx->L,
                     &node->loc,
                     "array initializer of type {t} cannot be assigned to "
                     "array of type '{t}'",
                     (FormatArg[]){{.t = init_}, {.t = type_}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
    }
    else if (!isTypeAssignableFrom(unThisType(type_), init_)) {
        bool allow =
            nodeIs(init, NullLit) && (isTupleType(type_) || isUnionType(type_));
        if (!allow) {
            logError(ctx->L,
                     &node->loc,
                     "variable initializer of type '{t}' is not assignable to "
                     "variable type '{t}'",
                     (FormatArg[]){{.t = init_}, {.t = type_}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
    }

    node->type = typeIs(type_, Auto) ? init_ : type_;
    if (typeIs(type_, Union) && type_ != init_) {
        u32 idx = findUnionTypeIndex(type_, init_);
        csAssert0(idx != UINT32_MAX);
        node->varDecl.init = makeUnionValueExpr(
            ctx->pool, &init->loc, init->flags, init, idx, NULL, type_);
    }

    if (hasFlag(node, Const) && !hasFlag(node->type, Const)) {
        node->type = makeWrappedType(ctx->types, node->type, flgConst);
    }

    if (!hasFlag(type_, Optional) || hasFlag(init_, Optional))
        return;

    const Type *target = getOptionalTargetType(type_);
    if (nodeIs(init, NullLit)) {
        if (!transformOptionalNone(visitor, init, target)) {
            node->type = ERROR_TYPE(ctx);
        }
    }
    else {
        init->type = target;
        if (!transformOptionalSome(
                visitor, init, copyAstNode(ctx->pool, init))) //
        {
            node->type = ERROR_TYPE(ctx);
        }
    }
}
