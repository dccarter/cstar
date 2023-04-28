//
// Created by Carter on 2023-04-26.
//

#pragma once

#include <lang/semantics.h>

void initEvalVisitor(AstVisitor *visitor, SemanticsContext *ctx);

bool evaluate(AstVisitor *visitor, AstNode *node);

f64 getNumericLiteral(const AstNode *node);
void setNumericLiteralValue(AstNode *node,
                            AstNode *lhs,
                            AstNode *rhs,
                            f64 value);
void evalStringConcatenation(SemanticsContext *ctx,
                             AstNode *node,
                             AstNode *lhs,
                             AstNode *rhs);

bool verifyBinaryExprOperand(SemanticsContext *ctx, AstNode *node);
bool evalBooleanCast(SemanticsContext *ctx, AstNode *node);

void evalPath(AstVisitor *visitor, AstNode *node);
void evalMemberExpr(AstVisitor *visitor, AstNode *node);
void evalIndexExpr(AstVisitor *visitor, AstNode *node);
void evalTupleExpr(AstVisitor *visitor, AstNode *node);
void evalArrayExpr(AstVisitor *visitor, AstNode *node);
void evalBinaryExpr(AstVisitor *visitor, AstNode *node);
void evalEnumDecl(AstVisitor *visitor, AstNode *node);

void evalIfStmt(AstVisitor *visitor, AstNode *node);
