//
// Created by Carter Mbotho on 2024-01-09.
//

#include "check.h"

#include "lang/frontend/flag.h"

static AstNode *getArrayDimension(AstNode *node)
{
    if (node == NULL)
        return node;

    switch (node->tag) {
    case astCastExpr:
        return node->castExpr.expr;
    case astTypedExpr:
        return node->typedExpr.expr;
    default:
        return node;
    }
}

AstNode *transformArrayExprToSliceCall(TypingContext *ctx,
                                       const Type *slice,
                                       AstNode *expr)
{
    return makeVarDecl(
        ctx->pool,
        &expr->loc,
        flgNone,
        makeAnonymousVariable(ctx->strings, "arr"),
        makeArrayTypeAstNode(ctx->pool,
                             &expr->loc,
                             flgNone,
                             makeTypeReferenceNode(ctx->pool,
                                                   getSliceTargetType(slice),
                                                   &expr->loc),
                             expr->type->array.len,
                             NULL,
                             NULL),
        expr,
        NULL,
        NULL);
}

void transformArrayExprToSlice(AstVisitor *visitor,
                               const Type *slice,
                               AstNode *expr)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr_ = shallowCloneAstNode(ctx->pool, expr);
    expr_->next = NULL;

    if (!nodeIsLeftValue(expr)) {
        AstNode *variable = makeVarDecl(
            ctx->pool,
            &expr->loc,
            expr->type->flags,
            makeAnonymousVariable(ctx->strings, "arr"),
            makeArrayTypeAstNode(
                ctx->pool,
                &expr->loc,
                expr->type->flags,
                makeTypeReferenceNode(
                    ctx->pool, getSliceTargetType(slice), &expr->loc),
                expr->type->array.len,
                NULL,
                expr->type),
            expr_,
            NULL,
            expr->type);
        astModifierAdd(&ctx->blockModifier, variable);
        expr_ = makeResolvedIdentifier(ctx->pool,
                                       &expr->loc,
                                       variable->varDecl.name,
                                       0,
                                       variable,
                                       NULL,
                                       variable->type);
    }

    const Type *type =
        makePointerType(ctx->types, expr->type->array.elementType, flgNone);

    expr_ =
        makeCastExpr(ctx->pool,
                     &expr->loc,
                     expr->type->flags,
                     expr_,
                     makeTypeReferenceNode(ctx->pool, type, &expr->loc),
                     makeIntegerLiteral(ctx->pool,
                                        &expr->loc,
                                        (i64)expr->type->array.len,
                                        NULL,
                                        getPrimitiveType(ctx->types, prtU64)),
                     type);

    clearAstBody(expr);
    expr->tag = astCallExpr;
    expr->type = NULL;
    expr->callExpr.callee = makeResolvedPath(ctx->pool,
                                             &expr->loc,
                                             slice->name,
                                             flgNone,
                                             slice->tStruct.decl,
                                             NULL,
                                             slice);
    expr->callExpr.args = expr_;

    // Lazy way of ensuring `init` is resolved correctly
    type = checkType(visitor, expr);
    csAssert0(!typeIs(type, Error));
}

void checkArrayType(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *element = checkType(visitor, node->arrayType.elementType);
    if (typeIs(element, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    u64 size = UINT64_MAX;
    const Type *dim_ = checkType(visitor, node->arrayType.dim);
    if (typeIs(dim_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }
    AstNode *dim = getArrayDimension(node->arrayType.dim);
    if (dim == NULL) {
        node->type = makeArrayType(ctx->types, element, size);
        return;
    }

    if (!nodeIs(dim, IntegerLit)) {
        logError(ctx->L,
                 &node->loc,
                 "expecting array dimension to be constant integral type "
                 "at compile time",
                 NULL);
        node->type = ERROR_TYPE(ctx);
        return;
    }
    size = integerLiteralValue(dim);
    node->type = makeArrayType(ctx->types, element, size);
}

void checkArrayExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    u64 count = 0;
    const Type *elementType = NULL;
    bool isLiteral = true;
    for (AstNode *elem = node->arrayExpr.elements; elem;
         elem = elem->next, count++) {
        const Type *type = checkType(visitor, elem);
        isLiteral = isLiteral && isLiteralExpr(elem);
        if (elementType == NULL) {
            elementType = type;
            continue;
        }

        if (!isTypeAssignableFrom(elementType, type)) {
            logError(ctx->L,
                     &elem->loc,
                     "inconsistent array types in array, expecting '{t}', "
                     "got '{t}'",
                     (FormatArg[]){{.t = elementType}, {.t = type}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
        // TODO maybe cast
        elem->type = elementType;
    }

    node->arrayExpr.isLiteral = isLiteral;
    if (elementType == NULL) {
        node->type = makeArrayType(ctx->types, makeAutoType(ctx->types), 0);
    }
    else {
        node->type = makeArrayType(ctx->types, elementType, count);
    }
}
