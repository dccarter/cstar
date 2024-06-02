//
// Created by Carter Mbotho on 2024-01-09.
//

#include "check.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/middle/builtins.h"

typedef enum {
    optInvalid = -1,
    optNumeric,
    optInteger,
    optLogical,
    optComparison,
    optEquality,
    optRange,
} BinaryOperatorKind;

static BinaryOperatorKind getBinaryOperatorKind(Operator op)
{
    switch (op) {
        // Numeric arithmetic
#define f(O, ...) case op##O:
        AST_ARITH_EXPR_LIST(f)
        return optNumeric;

        AST_BIT_EXPR_LIST(f)
        AST_SHIFT_EXPR_LIST(f)
        return optInteger;

        AST_LOGIC_EXPR_LIST(f)
        return optLogical;

        AST_CMP_EXPR_LIST(f)
        return (op == opEq || op == opNe) ? optEquality : optComparison;
#undef f
    case opRange:
        return optRange;
    default:
        unreachable("");
    }
}

static bool checkClassBinaryOperatorOverload(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    cstring name = getOpOverloadName(node->binaryExpr.op);
    const Type *target = stripPointer(node->binaryExpr.lhs->type);
    const NamedTypeMember *overload =
        findOverloadMemberUpInheritanceChain(target, name);

    const Type *rhs = checkType(visitor, node->binaryExpr.rhs);
    if (typeIs(rhs, Error)) {
        node->type = ERROR_TYPE(ctx);
        return false;
    }

    if (overload == NULL)
        return false;

    transformToMemberCallExpr(
        visitor, node, node->binaryExpr.lhs, name, node->binaryExpr.rhs);

    checkType(visitor, node);
    return true;
}

static void checkBinaryOperatorOverload(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);

    const Type *left = node->binaryExpr.lhs->type;
    cstring name = getOpOverloadName(node->binaryExpr.op);
    const Type *target = stripPointer(left);
    const NamedTypeMember *overload =
        findOverloadMemberUpInheritanceChain(stripAll(target), name);

    if (overload == NULL) {
        logError(ctx->L,
                 &node->loc,
                 "struct '{t}' does not not overload '{s}' binary operator",
                 (FormatArg[]){{.t = target},
                               {.s = getBinaryOpString(node->binaryExpr.op)}});
        node->type = ERROR_TYPE(ctx);
    }

    const Type *right = checkType(visitor, node->binaryExpr.rhs);
    if (typeIs(right, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    transformToMemberCallExpr(
        visitor, node, node->binaryExpr.lhs, name, node->binaryExpr.rhs);

    checkType(visitor, node);
}

void checkBinaryExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    const Type *left = checkType(visitor, lhs), *left_ = stripAll(left);

    Operator op = node->binaryExpr.op;
    BinaryOperatorKind opKind = getBinaryOperatorKind(op);

    if ((opKind == optComparison || opKind == optEquality) &&
        typeIs(unwrapType(left, NULL), String)) {
        lhs = node->binaryExpr.lhs;
        AstNode *cStringDecl = findBuiltinDecl(S_CString);
        node->binaryExpr.lhs = makeStructExpr(
            ctx->pool,
            &lhs->loc,
            flgNone,
            makeResolvedPath(ctx->pool,
                             &lhs->loc,
                             S_CString,
                             flgNone,
                             cStringDecl,
                             NULL,
                             cStringDecl->type),
            makeFieldExpr(ctx->pool, &lhs->loc, S_s, flgNone, lhs, NULL),
            NULL,
            cStringDecl->type);
        left_ = stripAll(checkType(visitor, node->binaryExpr.lhs));
    }

    if (typeIs(left_, Struct) && !hasFlag(left_->tStruct.decl, Extern)) {
        if (!nodeIs(rhs, NullLit) || !isPointerType(left)) {
            checkBinaryOperatorOverload(visitor, node);
            return;
        }
    }

    if (typeIs(left_, Class) && !nodeIs(rhs, NullLit)) {
        if (checkClassBinaryOperatorOverload(visitor, node))
            return;
        if (typeIs(node->type, Error))
            return;
    }

    node->binaryExpr.rhs->parentScope = node;
    const Type *right = checkType(visitor, node->binaryExpr.rhs);

    const Type *type = unwrapType(promoteType(ctx->types, left, right), NULL);

    if (type == NULL) {
        logError(ctx->L,
                 &node->loc,
                 "binary operation '{s}' between type '{t}' and '{t}' is not "
                 "supported",
                 (FormatArg[]){
                     {.s = getBinaryOpString(op)}, {.t = left}, {.t = right}});
        node->type = ERROR_TYPE(ctx);
        return;
    }
    node->type = type;

    switch (opKind) {
    case optNumeric:

        if (isNumericType(type) ||
            (isPointerType(type) && (op == opAdd || op == opSub)))
            break;

        logError(ctx->L,
                 &node->loc,
                 "cannot perform binary operation '{s}' on non-numeric "
                 "type '{t}'",
                 (FormatArg[]){{.s = getBinaryOpString(op)}, {.t = type}});
        node->type = ERROR_TYPE(ctx);
        return;

    case optInteger:
        if (!isIntegralType(type)) {
            logError(ctx->L,
                     &node->loc,
                     "cannot perform binary operation '{s}' on non-integeral "
                     "type '{t}'",
                     (FormatArg[]){{.s = getBinaryOpString(op)}, {.t = type}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
        break;

    case optLogical:
        if (!isBooleanType(type)) {
            logError(ctx->L,
                     &node->loc,
                     "cannot perform logical binary operation '{s}' on "
                     "non-boolean "
                     "type '{t}'",
                     (FormatArg[]){{.s = getBinaryOpString(op)}, {.t = type}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
        break;

    case optComparison:
        if (!isNumericType(type)) {
            logError(ctx->L,
                     &node->loc,
                     "cannot perform comparison binary operation '{s}' on "
                     "non-numeric "
                     "type '{t}'",
                     (FormatArg[]){{.s = getBinaryOpString(op)}, {.t = type}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
        node->type = getPrimitiveType(ctx->types, prtBool);
        break;

    case optEquality:
        switch (type->tag) {
        case typPrimitive:
        case typPointer:
        case typString:
        case typOpaque:
        case typEnum:
        case typFunc:
        case typClass:
            break;
        default:
            logError(ctx->L,
                     &node->loc,
                     "cannot perform equality binary operation '{s}' on "
                     "type '{t}'",
                     (FormatArg[]){{.s = getBinaryOpString(op)}, {.t = type}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
        node->type = getPrimitiveType(ctx->types, prtBool);
        break;

    case optRange: {
        if (!isIntegralType(left)) {
            logError(ctx->L,
                     &node->loc,
                     "expecting an integral type for range expression "
                     "start, got "
                     "type '{t}'",
                     (FormatArg[]){{.t = left}});
            node->type = ERROR_TYPE(ctx);
            return;
        }

        if (!isIntegralType(right)) {
            logError(ctx->L,
                     &node->loc,
                     "expecting an integral type for range expression end, got "
                     "type '{t}'",
                     (FormatArg[]){{.t = left}});
            node->type = ERROR_TYPE(ctx);
            return;
        }

        AstNode binary = *node;
        clearAstBody(node);
        node->tag = astRangeExpr;
        node->rangeExpr.start = binary.binaryExpr.lhs;
        node->rangeExpr.end = binary.binaryExpr.rhs;
        node->rangeExpr.step = NULL;
        node->type = getPrimitiveType(ctx->types, prtI64);
        break;
    }
    default:
        unreachable("");
    }
}
