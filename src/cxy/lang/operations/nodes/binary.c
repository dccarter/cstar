//
// Created by Carter Mbotho on 2023-08-03.
//

#include "../check.h"
#include "../codegen.h"

#include "lang/flag.h"
#include "lang/operations.h"
#include "lang/strings.h"
#include "lang/ttable.h"
#include "lang/types.h"
#include "lang/visitor.h"

#include "core/alloc.h"
#include "lang/ast.h"

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

static void checkBinaryOperatorOverload(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);

    const Type *left = node->binaryExpr.lhs->type;
    cstring name = getOpOverloadName(node->binaryExpr.op);
    const Type *target = stripPointer(left);
    const StructMember *overload = findStructMember(target, name);

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

void generateBinaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->binaryExpr.lhs);
    format(ctx->state,
           " {s} ",
           (FormatArg[]){{.s = getBinaryOpString(node->binaryExpr.op)}});
    astConstVisit(visitor, node->binaryExpr.rhs);
}

void checkBinaryExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *left = checkType(visitor, node->binaryExpr.lhs);
    if (typeIs(left, This))
        left = left->this.that;

    if (stripAll(left)->tag == typStruct) {
        checkBinaryOperatorOverload(visitor, node);
        return;
    }

    Operator op = node->binaryExpr.op;
    const Type *right = checkType(visitor, node->binaryExpr.rhs);
    BinaryOperatorKind opKind = getBinaryOperatorKind(op);

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
        if (!isNumericType(type)) {
            logError(ctx->L,
                     &node->loc,
                     "cannot perform binary operation '{s}' on non-numeric "
                     "type '{t}'",
                     (FormatArg[]){{.s = getBinaryOpString(op)}, {.t = type}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
        break;

    case optInteger:
        if (!isIntegerType(type)) {
            logError(ctx->L,
                     &node->loc,
                     "cannot perform binary operation '{s}' on non-integer "
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
            return;
            node->type = ERROR_TYPE(ctx);
        }
        node->type = getPrimitiveType(ctx->types, prtBool);
        break;

    case optEquality:
        if (!typeIs(type, Primitive) && !typeIs(type, Pointer) &&
            !typeIs(type, String) && !typeIs(type, Enum)) {
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
