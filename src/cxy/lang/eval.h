//
// Created by Carter on 2023-04-26.
//

#pragma once

#include <core/sb.h>
#include <lang/semantics.h>

void initEvalVisitor(AstVisitor *visitor, SemanticsContext *ctx);

bool evaluate(AstVisitor *visitor, AstNode *node);
void initComptime(SemanticsContext *ctx);

f64 getNumericLiteral(const AstNode *node);
void setNumericLiteralValue(AstNode *node,
                            AstNode *lhs,
                            AstNode *rhs,
                            f64 value);
void evalStringConcatenation(SemanticsContext *ctx,
                             AstNode *node,
                             AstNode *lhs,
                             AstNode *rhs);
bool evalStringBuilderAppend(SemanticsContext *ctx,
                             StringBuilder *sb,
                             AstNode *node);

AstNode *evalAstNodeMemberAccess(SemanticsContext *ctx,
                                 const FileLoc *loc,
                                 AstNode *node,
                                 cstring name);

bool verifyBinaryExprOperand(SemanticsContext *ctx, AstNode *node);
bool evalBooleanCast(SemanticsContext *ctx, AstNode *node);

void evalPath(AstVisitor *visitor, AstNode *node);
void evalMemberExpr(AstVisitor *visitor, AstNode *node);
void evalIndexExpr(AstVisitor *visitor, AstNode *node);
void evalTupleExpr(AstVisitor *visitor, AstNode *node);
void evalArrayExpr(AstVisitor *visitor, AstNode *node);
void evalBinaryExpr(AstVisitor *visitor, AstNode *node);
void evalUnaryExpr(AstVisitor *visitor, AstNode *node);
void evalAssignExpr(AstVisitor *visitor, AstNode *node);
void evalEnumDecl(AstVisitor *visitor, AstNode *node);
void evalVarDecl(AstVisitor *visitor, AstNode *node);
void evalMacroCall(AstVisitor *visitor, AstNode *node);

void evalIfStmt(AstVisitor *visitor, AstNode *node);
void evalForStmt(AstVisitor *visitor, AstNode *node);
