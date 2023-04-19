//
// Created by Carter on 2023-04-18.
//

#include "lang/codegen.h"
#include "lang/semantics.h"

#include "lang/ttable.h"

#include "core/alloc.h"

#include <memory.h>

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

    case opRange:
        return optRange;
    default:
        unreachable("");
    }
}

static void checkBinaryOperatorOverload(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    const Type *left = node->binaryExpr.lhs->type;
    cstring name = getBinaryOpFuncName(node->binaryExpr.op);
    const Type *target = stripPointer(left);
    AstNode *overload = findSymbolOnly(target->tStruct.env, name);

    if (overload == NULL) {
        logError(ctx->L,
                 &node->binaryExpr.lhs->loc,
                 "binary operator '{s}' no overloaded on struct type '{t}'",
                 (FormatArg[]){{.s = getBinaryOpString(node->binaryExpr.op)},
                               {.t = target}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    transformToMemberCallExpr(
        ctx, node, overload, node->binaryExpr.lhs, name, node->binaryExpr.rhs);
    evalType(visitor, node);
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
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *left = evalType(visitor, node->binaryExpr.lhs);
    if (stripPointer(left)->tag == typStruct) {
        checkBinaryOperatorOverload(visitor, node);
        return;
    }

    Operator op = node->binaryExpr.op;
    const Type *right = evalType(visitor, node->binaryExpr.rhs);
    BinaryOperatorKind opKind = getBinaryOperatorKind(op);

    const Type *type = promoteType(ctx->typeTable, left, right);
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
        if (type != getPrimitiveType(ctx->typeTable, prtBool)) {
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
        node->type = getPrimitiveType(ctx->typeTable, prtBool);
        break;

    case optEquality:
        if (type->tag != typPrimitive && type->tag != typPointer &&
            type->tag != typString) {
            logError(ctx->L,
                     &node->loc,
                     "cannot perform equality binary operation '{s}' on "
                     "type '{t}'",
                     (FormatArg[]){{.s = getBinaryOpString(op)}, {.t = type}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
        node->type = getPrimitiveType(ctx->typeTable, prtBool);
        break;

    case optRange: {
        if (!isIntegerType(left)) {
            logError(ctx->L,
                     &node->loc,
                     "expecting an integral type for range expression "
                     "start, got "
                     "type '{t}'",
                     (FormatArg[]){{.t = left}});
            node->type = ERROR_TYPE(ctx);
            return;
        }

        if (!isIntegerType(right)) {
            logError(ctx->L,
                     &node->loc,
                     "expecting an integral type for range expression end, got "
                     "type '{t}'",
                     (FormatArg[]){{.t = left}});
            node->type = ERROR_TYPE(ctx);
            return;
        }

        AstNode binary = *node;
        memset(&node->binaryExpr, 0, sizeof(node->binaryExpr));
        node->tag = astRangeExpr;
        node->rangeExpr.start = binary.binaryExpr.lhs;
        node->rangeExpr.end = binary.binaryExpr.rhs;
        node->rangeExpr.step = NULL;
        node->type = makeOpaqueType(ctx->typeTable, "cxy_range_t");
        break;
    }
    default:
        unreachable("");
    }
}
