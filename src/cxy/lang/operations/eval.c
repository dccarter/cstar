//
// Created by Carter on 2023-04-26.
//

#include "eval.h"

#include "lang/flag.h"
#include "lang/ttable.h"

static void evalPrefixMinusOperation(EvalContext *ctx, AstNode *node)
{
    AstNode *rhs = node->unaryExpr.operand;
    if (nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operand of prefix `-` cannot be of type string",
                 NULL);

        node->tag = astError;
        return;
    }

    if (nodeIs(rhs, BoolLit)) {
        logWarning(
            ctx->L,
            &node->loc,
            "comp-time prefix `-` operation with boolean operands doesn't "
            "make sense",
            NULL);
    }
    else if (nodeIs(rhs, CharLit)) {
        logWarning(
            ctx->L,
            &node->loc,
            "comp-time prefix `-` operation with character operands doesn't "
            "make sense",
            NULL);
    }

    f64 value = -getNumericLiteral(rhs);
    if (nodeIs(node, FloatLit)) {
        node->tag = astFloatLit;
        node->floatLiteral.value = value;
    }
    else {
        node->tag = astIntegerLit;
        node->intLiteral.value = value;
    }
}

static void evalPrefixPlusOperation(EvalContext *ctx, AstNode *node)
{
    AstNode *rhs = node->unaryExpr.operand;
    if (nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operand of prefix `+` cannot be of type string",
                 NULL);

        node->tag = astError;
        return;
    }

    if (nodeIs(rhs, BoolLit)) {
        logWarning(
            ctx->L,
            &node->loc,
            "comp-time prefix `+` operation with boolean operands doesn't "
            "make sense",
            NULL);
    }
    else if (nodeIs(rhs, CharLit)) {
        logWarning(
            ctx->L,
            &node->loc,
            "comp-time prefix `+` operation with character operands doesn't "
            "make sense",
            NULL);
    }

    f64 value = +getNumericLiteral(rhs);
    if (nodeIs(node, FloatLit)) {
        node->tag = astFloatLit;
        node->floatLiteral.value = value;
    }
    else {
        node->tag = astIntegerLit;
        node->intLiteral.value = value;
    }
}

static void evalLogicNotOperation(EvalContext *ctx, AstNode *node)
{
    AstNode *rhs = node->unaryExpr.operand;
    if (nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operand of prefix `!` cannot be of type string",
                 NULL);

        node->tag = astError;
        return;
    }

    node->tag = astBoolLit;
    node->boolLiteral.value = !getNumericLiteral(rhs);
}

static void evalBitNotOperation(EvalContext *ctx, AstNode *node)
{
    AstNode *rhs = node->binaryExpr.lhs;
    if (nodeIs(rhs, StringLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `~` cannot be of type string",
                 NULL);

        node->tag = astError;
        return;
    }

    if (nodeIs(rhs, FloatLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comp-time operands of `~` cannot be float",
                 NULL);

        node->tag = astError;
        return;
    }

    if (nodeIs(rhs, BoolLit)) {
        logWarning(ctx->L,
                   &node->loc,
                   "comp-time binary bit not with boolean operands doesn't "
                   "make sense",
                   NULL);
    }
    else if (nodeIs(rhs, CharLit)) {
        logWarning(ctx->L,
                   &node->loc,
                   "comp-time binary bit not with character operands doesn't "
                   "make sense",
                   NULL);
    }

    node->tag = astIntegerLit;
    node->intLiteral.value = ~((i64)getNumericLiteral(rhs));
}

static void evalGroupExpr(AstVisitor *visitor, AstNode *node)
{
    if (!evaluate(visitor, node->groupExpr.expr)) {
        node->tag = astError;
        return;
    }

    replaceAstNodeWith(node, node->groupExpr.expr);
    node->flags &= ~flgComptime;
}

static void evalFallback(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    logError(ctx->L, &node->loc, "unsupported compile time expression", NULL);
    node->tag = astError;
}

const Type *evalType(EvalContext *ctx, AstNode *node)
{
    astVisit(ctx->typer, node);
    return node->type;
}

bool evaluate(AstVisitor *visitor, AstNode *node)
{
    if (isLiteralExpr(node) || isBuiltinTypeExpr(node))
        return true;

    astVisit(visitor, node);
    return !nodeIs(node, Error);
}

void initEvalVisitor(AstVisitor *visitor, EvalContext *ctx)
{
    // clang-format off
    *visitor = makeAstVisitor(ctx, {
        [astPath] = evalPath,
        [astIfStmt] = evalIfStmt,
        [astForStmt] = evalForStmt,
        [astTupleExpr] = evalTupleExpr,
        [astBinaryExpr] = evalBinaryExpr,
        [astAssignExpr] = evalAssignExpr,
        [astUnaryExpr] = evalUnaryExpr,
        [astIndexExpr] = evalIndexExpr,
        [astMemberExpr] = evalMemberExpr,
        [astArrayExpr]= evalArrayExpr,
        [astGroupExpr] = evalGroupExpr,
        [astEnumDecl] = evalEnumDecl,
        [astMacroCallExpr] = evalMacroCall,
        [astVarDecl] = evalVarDecl
    }, .fallback = evalFallback);

    initComptime(ctx);
}
