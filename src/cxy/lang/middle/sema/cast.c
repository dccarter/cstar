//
// Created by Carter Mbotho on 2024-01-09.
//

#include "../eval/eval.h"
#include "check.h"

#include "lang/frontend/flag.h"

static void castIntegerLiteral(const Type *to, AstNode *expr)
{
#define CAST_INTEGER(TYP)                                                      \
    if (expr->intLiteral.isNegative)                                           \
        expr->intLiteral.value = (i64)(TYP)expr->intLiteral.value;             \
    else                                                                       \
        expr->intLiteral.uValue = (u64)(TYP)expr->intLiteral.uValue;

    switch (to->primitive.id) {
    case prtU8:
        CAST_INTEGER(u8);
        break;
    case prtI8:
        CAST_INTEGER(i8);
        break;
    case prtU16:
        CAST_INTEGER(u16);
        break;
    case prtI16:
        CAST_INTEGER(i16);
        break;
    case prtU32:
        CAST_INTEGER(u32);
        break;
    case prtI32:
        CAST_INTEGER(i32);
        break;
    case prtU64:
        CAST_INTEGER(u64);
        break;
    case prtI64:
        CAST_INTEGER(i64);
        break;
    default:
        break;
    }
#undef CAST_INTEGER
}

static void castLiteral(TypingContext *ctx, AstNode *node)
{
    if (isLiteralExpr(node) && typeIs(node->type, Primitive)) {
        AstNode *expr = node->castExpr.expr;
        expr->type = node->type;
        if (isIntegerType(node->type)) {
            if (nodeIs(expr, FloatLit)) {
                expr->tag = astIntegerLit;
                if (expr->floatLiteral.value < 0) {
                    expr->intLiteral.isNegative = true;
                    expr->intLiteral.value = (i64)expr->floatLiteral.value;
                }
                else {
                    expr->intLiteral.isNegative = false;
                    expr->intLiteral.uValue = (u64)expr->floatLiteral.value;
                }
            }
            else if (nodeIs(expr, CharLit)) {
                expr->tag = astIntegerLit;
                expr->intLiteral.isNegative = false;
                expr->intLiteral.uValue = expr->charLiteral.value;
            }
            else if (nodeIs(expr, BoolLit)) {
                expr->tag = astIntegerLit;
                expr->intLiteral.isNegative = false;
                expr->intLiteral.uValue = expr->boolLiteral.value ? 1 : 0;
            }
            else if (isEnumLiteral(expr)) {
                i64 value = getEnumLiteralValue(expr);
                expr->tag = astIntegerLit;
                if (value < 0) {
                    expr->intLiteral.isNegative = true;
                    expr->intLiteral.value = value;
                }
                else {
                    expr->intLiteral.isNegative = false;
                    expr->intLiteral.uValue = value;
                }
            }
            else {
                csAssert0(nodeIs(expr, IntegerLit));
            }
            castIntegerLiteral(node->type, expr);
            replaceAstNodeWith(node, expr);
        }
        else if (isFloatType(node->type)) {
            if (!nodeIs(expr, FloatLit)) {
                expr->floatLiteral.value = (f64)integerLiteralValue(expr);
                expr->tag = astFloatLit;
            }
            if (node->type->primitive.id == prtF32)
                expr->floatLiteral.value = (f32)(expr->floatLiteral.value);
            replaceAstNodeWith(node, expr);
        }
        else if (isCharacterType(node->type)) {
            expr->tag = astCharLit;
            expr->charLiteral.value = (i32)nodeGetNumericLiteral(expr);
            if (node->primitiveType.id == prtCChar)
                expr->charLiteral.value = (u8)expr->charLiteral.value;
            replaceAstNodeWith(node, expr);
        }
        else {
            unreachable("UNSUPPORTED CAST");
        }
    }
}

void checkCastExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *expr = checkType(visitor, node->castExpr.expr);
    const Type *target = checkType(visitor, node->castExpr.to);
    if (!isTypeCastAssignable(target, expr)) {
        logError(ctx->L,
                 &node->loc,
                 "type '{t}' cannot be cast to type '{t}'",
                 (FormatArg[]){{.t = expr}, {.t = target}});
    }
    node->type = target;
    if (typeIs(expr, Union)) {
        node->castExpr.idx = findUnionTypeIndex(
            expr, typeIs(target, Pointer) ? target->pointer.pointed : target);
        csAssert0(node->castExpr.idx != UINT32_MAX);
    }

    castLiteral(ctx, node);
    if (isReferenceType(expr))
        expr = stripReference(expr);
    if (!hasFlag(target, Optional) || hasFlag(expr, Optional))
        return;

    target = getOptionalTargetType(target);
    if (nodeIs(node->castExpr.expr, NullLit)) {
        if (!transformOptionalNone(visitor, node, target)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }
    }
    else {
        node->castExpr.expr->type = target;
        if (!transformOptionalSome(visitor, node, node->castExpr.expr)) //
        {
            node->type = ERROR_TYPE(ctx);
            return;
        }
    }
}

void checkTypedExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *expr = checkType(visitor, node->typedExpr.expr);
    if (typeIs(expr, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    const Type *type = checkType(visitor, node->typedExpr.type);
    if (isTypeCastAssignable(type, expr) ||
        (isPointerType(expr) &&
         (isPointerType(type) || isIntegerType(type))) || //
        (expr->tag == typPrimitive && expr->primitive.id == prtU64 &&
         isVoidPointer(type))) {
        node->type = type;
    }
    else {
        logError(ctx->L,
                 &node->loc,
                 "type '{t}' cannot be cast to type '{t}'",
                 (FormatArg[]){{.t = expr}, {.t = type}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    castLiteral(ctx, node);
    if (typeIs(expr, Union)) {
        node->castExpr.idx = findUnionTypeIndex(
            expr, typeIs(type, Pointer) ? type->pointer.pointed : type);
        csAssert0(node->castExpr.idx != UINT32_MAX);
    }

    if (!hasFlag(type, Optional) || hasFlag(expr, Optional))
        return;

    type = getOptionalTargetType(type);
    if (nodeIs(node->typedExpr.expr, NullLit)) {
        if (!transformOptionalNone(visitor, node, type))
            node->type = ERROR_TYPE(ctx);
    }
    else {
        node->typedExpr.expr->type = type;
        if (!transformOptionalSome(visitor, node, node->typedExpr.expr))
            node->type = ERROR_TYPE(ctx);
    }
}