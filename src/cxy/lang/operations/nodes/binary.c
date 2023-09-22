//
// Created by Carter Mbotho on 2023-08-03.
//

#include "../check.h"
#include "../codegen.h"
#include "../eval.h"

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

static inline bool isSupportedBinaryOperand(AstNode *node)
{
    node = nodeIs(node, Ref) ? node->reference.target : node;
    return isLiteralExpr(node) || isTypeExpr(node);
}

static inline bool isArgumentTypeThis(const AstNode *node)
{
    return nodeIs(node, Path) &&
           node->path.elements->pathElement.name == S_This;
}

bool verifyBinaryExprOperand(EvalContext *ctx, AstNode *node)
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

static void evalAddOperation(EvalContext *ctx, AstNode *node)
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

static void evalSubOperation(EvalContext *ctx, AstNode *node)
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

static void evalMulOperation(EvalContext *ctx, AstNode *node)
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

static void evalDivOperation(EvalContext *ctx, AstNode *node)
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

static void evalModOperation(EvalContext *ctx, AstNode *node)
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

static bool checkBinaryBitOperation(EvalContext *ctx, AstNode *node)
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
    static void eval##Name##Operation(EvalContext *ctx, AstNode *node)         \
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

static void evalLAndOperation(EvalContext *ctx, AstNode *node)
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

static void evalLOrOperation(EvalContext *ctx, AstNode *node)
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

static bool checkComparisonOperation(EvalContext *ctx, AstNode *node)
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
    static void eval##Name##Operation(EvalContext *ctx, AstNode *node)         \
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
            node->boolLiteral.value = true OP comptimeCompareTypes(lhs, rhs);  \
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
    static void eval##Name##Operation(EvalContext *ctx, AstNode *node)         \
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

static void evalRangeOperation(EvalContext *ctx, AstNode *node)
{
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    if (nodeIs(lhs, IntegerLit) && nodeIs(rhs, IntegerLit)) {
        node->tag = astRangeExpr;
        node->rangeExpr.start = lhs;
        node->rangeExpr.end = rhs;
        node->rangeExpr.step = NULL;
        return;
    }

    logError(ctx->L,
             &node->loc,
             "unsupported comp-time operation '..', range start and stop must "
             "be compile time expressions",
             NULL);

    node->tag = astError;
}

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
    const NamedTypeMember *overload = findStructMember(target, name);

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

void evalBinaryExpr(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    AstNode *next = node->next, *parentScope = node->parentScope;
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

    evalType(ctx, node->binaryExpr.rhs);
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

    node->next = next;
    node->parentScope = parentScope;
}
