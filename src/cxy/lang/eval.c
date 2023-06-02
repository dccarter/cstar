//
// Created by Carter on 2023-04-26.
//

#include "lang/semantics.h"

#include "lang/eval.h"
#include "lang/ttable.h"

#include <string.h>

static void evalPrefixMinusOperation(SemanticsContext *ctx, AstNode *node)
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

static void evalPrefixPlusOperation(SemanticsContext *ctx, AstNode *node)
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

static void evalLogicNotOperation(SemanticsContext *ctx, AstNode *node)
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

static void evalBitNotOperation(SemanticsContext *ctx, AstNode *node)
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

static void evalFallback(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    logError(ctx->L, &node->loc, "unsupported compile time expression", NULL);
    node->tag = astError;
}

bool evaluate(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    if (isLiteralExpr(node) || isBuiltinTypeExpr(node))
        return true;

    astVisit(visitor, node);
    return node->tag != astError;
}

void initEvalVisitor(AstVisitor *visitor, SemanticsContext *ctx)
{
    // clang-format off
    *visitor = makeAstVisitor(ctx, {
        [astPath] = evalPath,
        [astIfStmt] = evalIfStmt,
        [astForStmt] = evalForStmt,
        [astTupleExpr] = evalTupleExpr,
        [astBinaryExpr] = evalBinaryExpr,
        [astAssignExpr] = evalAssignExpr,
        [astIndexExpr] = evalIndexExpr,
        [astMemberExpr] = evalMemberExpr,
        [astArrayExpr]= evalArrayExpr,
        [astEnumDecl] = evalEnumDecl,
        [astMacroCallExpr] = evalMacroCall,
        [astVarDecl] = evalVarDecl
    }, .fallback = evalFallback);

    // clang-format on

    ctx->eval.visitor = visitor;
    environmentInit(&ctx->eval.env);
    environmentAttachUp(&ctx->eval.env, ctx->env);
    pushScope(&ctx->eval.env, NULL);

    initComptime(ctx);
}