//
// Created by Carter on 2023-04-18.
//

#include "lang/codegen.h"
#include "lang/semantics.h"

#include "lang/ttable.h"

#include "core/alloc.h"
#include "lang/eval.h"

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
#undef f
    case opRange:
        return optRange;
    default:
        unreachable("");
    }
}

static bool checkBinaryOperatorOverload(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    const Type *left = node->binaryExpr.lhs->type;
    cstring name = getBinaryOpFuncName(node->binaryExpr.op);
    const Type *target = stripPointer(left);
    AstNode *overload = findSymbolOnly(target->tStruct.env, name);

    if (overload == NULL) {
        return false;
    }

    transformToMemberCallExpr(visitor,
                              node,
                              overload,
                              node->binaryExpr.lhs,
                              name,
                              node->binaryExpr.rhs);
    evalType(visitor, node);

    return true;
}

static inline bool isSupportedBinaryOperand(AstNode *node)
{
    return isLiteralExpr(node) || isTypeExpr(node);
}

bool verifyBinaryExprOperand(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs;
    AstNode *rhs = node->binaryExpr.rhs;
    if (!isSupportedBinaryOperand(lhs) || !isSupportedBinaryOperand(rhs)) {
        logError(ctx->L,
                 &lhs->loc,
                 "operands of `{s}` must be either a literal or type at "
                 "compile time",
                 (FormatArg[]){{.s = getBinaryOpString(node->binaryExpr.op)}});
        node->tag = astError;
        return false;
    }

    return true;
}

static void evalAddOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    if (nodeIs(lhs, StringLit)) {
        evalStringConcatenation(ctx, node, lhs, rhs);
        return;
    }

    if (nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "RHS operand of comp-time `+` operation can only "
                 "be a string when LHS is a string",
                 NULL);

        node->tag = astError;
        return;
    }

    if (nodeIs(lhs, BoolLit) && nodeIs(rhs, BoolLit)) {
        logWarning(
            ctx->L,
            &node->loc,
            "comp-time addition between boolean operands doesn't make sense",
            NULL);

        node->tag = astError;
        return;
    }

    f64 value = getNumericLiteral(lhs) + getNumericLiteral(rhs);
    setNumericLiteralValue(node, lhs, rhs, value);
}

static void evalSubOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `-` cannot be a string",
                 NULL);

        node->tag = astError;
        return;
    }

    if (nodeIs(lhs, BoolLit) && nodeIs(rhs, BoolLit)) {
        logWarning(
            ctx->L,
            &node->loc,
            "comp-time subtraction between boolean operands doesn't make sense",
            NULL);
    }

    f64 value = getNumericLiteral(lhs) - getNumericLiteral(rhs);
    setNumericLiteralValue(node, lhs, rhs, value);
}

static void evalMulOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `*` cannot be a string",
                 NULL);

        node->tag = astError;
        return;
    }

    if (nodeIs(lhs, BoolLit) || nodeIs(rhs, BoolLit)) {
        logWarning(ctx->L,
                   &node->loc,
                   "comp-time multiplication with boolean operands doesn't "
                   "make sense",
                   NULL);
    }
    else if (nodeIs(lhs, CharLit) || nodeIs(rhs, CharLit)) {
        logWarning(ctx->L,
                   &node->loc,
                   "comp-time multiplication with character operands doesn't "
                   "make sense",
                   NULL);
    }

    f64 value = getNumericLiteral(lhs) * getNumericLiteral(rhs);
    setNumericLiteralValue(node, lhs, rhs, value);
}

static void evalDivOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `/` cannot be a string",
                 NULL);

        node->tag = astError;
        return;
    }

    if (nodeIs(lhs, BoolLit) || nodeIs(rhs, BoolLit)) {
        logWarning(ctx->L,
                   &node->loc,
                   "comp-time division with boolean operands doesn't "
                   "make sense",
                   NULL);
    }
    else if (nodeIs(lhs, CharLit) || nodeIs(rhs, CharLit)) {
        logWarning(ctx->L,
                   &node->loc,
                   "comp-time division with character operands doesn't "
                   "make sense",
                   NULL);
    }

    if (nodeIs(lhs, FloatLit) || nodeIs(rhs, FloatLit)) {
        node->tag = astFloatLit;
        node->floatLiteral.value =
            getNumericLiteral(lhs) / getNumericLiteral(rhs);
    }
    else {
        i64 value = (i64)getNumericLiteral(lhs) / (i64)getNumericLiteral(rhs);
        setNumericLiteralValue(node, lhs, rhs, (f64)value);
    }
}

static void evalModOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `%` cannot be a string",
                 NULL);

        node->tag = astError;
        return;
    }

    if (nodeIs(lhs, FloatLit) || nodeIs(rhs, FloatLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operand(s) of `%` cannot be float",
                 NULL);

        node->tag = astError;
        return;
    }

    setNumericLiteralValue(
        node,
        lhs,
        rhs,
        (f64)((i64)getNumericLiteral(lhs) % (i64)getNumericLiteral(rhs)));
}

static bool checkBinaryBitOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    cstring name = getBinaryOpString(node->binaryExpr.op);
    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `{s}` cannot be of type string",
                 (FormatArg[]){{.s = name}});

        node->tag = astError;
        return false;
    }

    if (nodeIs(lhs, FloatLit) || nodeIs(rhs, FloatLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `{s}` cannot be float",
                 (FormatArg[]){{.s = name}});

        node->tag = astError;
        return false;
    }

    if (nodeIs(lhs, BoolLit) || nodeIs(rhs, BoolLit)) {
        logWarning(
            ctx->L,
            &node->loc,
            "comp-time binary operator `{s}' with boolean operands doesn't "
            "make sense",
            (FormatArg[]){{.s = name}});
    }
    else if (nodeIs(lhs, CharLit) || nodeIs(rhs, CharLit)) {
        logWarning(
            ctx->L,
            &node->loc,
            "comp-time binary operator '{s}' with character operands doesn't "
            "make sense",
            (FormatArg[]){{.s = name}});
    }

    return true;
}

#define CXY_DEFINE_BINARY_BIT_OPERATOR(Name, OP)                               \
    static void eval##Name##Operation(SemanticsContext *ctx, AstNode *node)    \
    {                                                                          \
        AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;      \
        if (!checkBinaryBitOperation(ctx, node))                               \
            return;                                                            \
                                                                               \
        setNumericLiteralValue(node,                                           \
                               lhs,                                            \
                               rhs,                                            \
                               (f64)((i64)getNumericLiteral(lhs) OP(i64)       \
                                         getNumericLiteral(rhs)));             \
    }

CXY_DEFINE_BINARY_BIT_OPERATOR(Shl, <<)
CXY_DEFINE_BINARY_BIT_OPERATOR(Shr, >>)
CXY_DEFINE_BINARY_BIT_OPERATOR(BAnd, &)
CXY_DEFINE_BINARY_BIT_OPERATOR(BOr, |)
CXY_DEFINE_BINARY_BIT_OPERATOR(BXor, ^)

static void evalLAndOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `&&` cannot be of type string",
                 NULL);

        node->tag = astError;
        return;
    }

    node->tag = astBoolLit;
    node->boolLiteral.value = getNumericLiteral(lhs) && getNumericLiteral(rhs);
}

static void evalLOrOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `&&` cannot be of type string",
                 NULL);

        node->tag = astError;
        return;
    }

    node->tag = astBoolLit;
    node->boolLiteral.value = getNumericLiteral(lhs) || getNumericLiteral(rhs);
}

static bool checkComparisonOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "unsupported comp-time operation '{s}' between a string and a "
                 "non-string literal",
                 (FormatArg[]){{.s = getBinaryOpString(node->binaryExpr.op)}});

        node->tag = astError;
        return false;
    }

    if (isTypeExpr(lhs) || isTypeExpr(rhs)) {
        logError(ctx->L,
                 &node->loc,
                 "unsupported comp-time operation '{s}' between a type and a "
                 "non type literal",
                 (FormatArg[]){{.s = getBinaryOpString(node->binaryExpr.op)}});

        node->tag = astError;
        return false;
    }

    return true;
}

#define CXY_DEFINE_BINARY_EQ_COMP_OPERATOR(Name, OP)                           \
    static void eval##Name##Operation(SemanticsContext *ctx, AstNode *node)    \
    {                                                                          \
        AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;      \
        if (nodeIs(lhs, StringLit) && nodeIs(rhs, StringLit)) {                \
            memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);                   \
            node->tag = astBoolLit;                                            \
            node->boolLiteral.value =                                          \
                lhs->stringLiteral.value OP rhs->stringLiteral.value;          \
            return;                                                            \
        }                                                                      \
        if (isTypeExpr(lhs) && isTypeExpr(rhs)) {                              \
            memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);                   \
            node->tag = astBoolLit;                                            \
            node->boolLiteral.value = comptimeCompareTypes(lhs, rhs);          \
            return;                                                            \
        }                                                                      \
                                                                               \
        if (!checkComparisonOperation(ctx, node))                              \
            return;                                                            \
                                                                               \
        memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);                       \
        node->tag = astBoolLit;                                                \
        node->boolLiteral.value =                                              \
            getNumericLiteral(lhs) OP getNumericLiteral(rhs);                  \
    }

CXY_DEFINE_BINARY_EQ_COMP_OPERATOR(Eq, ==)
CXY_DEFINE_BINARY_EQ_COMP_OPERATOR(Ne, !=)

#undef CXY_DEFINE_BINARY_EQ_COMP_OPERATOR

#define CXY_DEFINE_BINARY_COMP_OPERATOR(Name, OP)                              \
    static void eval##Name##Operation(SemanticsContext *ctx, AstNode *node)    \
    {                                                                          \
        AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;      \
        if (nodeIs(lhs, StringLit) && nodeIs(rhs, StringLit)) {                \
            memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);                   \
            node->tag = astBoolLit;                                            \
            node->boolLiteral.value = strcmp(lhs->stringLiteral.value,         \
                                             rhs->stringLiteral.value) OP 0;   \
            return;                                                            \
        }                                                                      \
                                                                               \
        if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {                \
            logError(ctx->L,                                                   \
                     &node->loc,                                               \
                     "unsupported comp-time operation " #OP                    \
                     " between a string and a "                                \
                     "non-string literal",                                     \
                     NULL);                                                    \
                                                                               \
            node->tag = astError;                                              \
            return;                                                            \
        }                                                                      \
                                                                               \
        memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);                       \
        node->tag = astBoolLit;                                                \
        node->boolLiteral.value =                                              \
            getNumericLiteral(lhs) OP getNumericLiteral(rhs);                  \
    }

CXY_DEFINE_BINARY_COMP_OPERATOR(Lt, <)
CXY_DEFINE_BINARY_COMP_OPERATOR(Gt, >)
CXY_DEFINE_BINARY_COMP_OPERATOR(Leq, <=)
CXY_DEFINE_BINARY_COMP_OPERATOR(Geq, >=)

#undef CXY_DEFINE_BINARY_COMP_OPERATOR

static void evalRangeOperation(SemanticsContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    if (nodeIs(lhs, StringLit) || nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "unsupported comp-time operation '<=' between a string and a "
                 "non-string literal",
                 NULL);

        node->tag = astError;
        return;
    }
    unreachable("TODO");
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
    if (typeIs(left, This))
        left = left->this.that;

    if (stripPointer(left)->tag == typStruct) {
        if (checkBinaryOperatorOverload(visitor, node))
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

void evalBinaryExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    if (!evaluate(visitor, node->binaryExpr.lhs)) {
        node->tag = astError;
        return;
    }

    if (!evaluate(visitor, node->binaryExpr.rhs)) {
        node->tag = astError;
        return;
    }

    if (!verifyBinaryExprOperand(ctx, node))
        return;

    switch (node->binaryExpr.op) {
#define f(O, ...)                                                              \
    case op##O:                                                                \
        eval##O##Operation(ctx, node);                                         \
        break;
        AST_BINARY_EXPR_LIST(f)
#undef f
    default:
        unreachable("NOT supported binary operator")
    }
}
