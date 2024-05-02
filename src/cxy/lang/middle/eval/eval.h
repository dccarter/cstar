//
// Created by Carter on 2023-04-26.
//

#pragma once

#include "core/sb.h"

#include "lang/frontend/visitor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Log *L;
    MemPool *pool;
    struct StrPool *strings;
    TypeTable *types;
    AstVisitor *typer;
} EvalContext;

void initEvalVisitor(AstVisitor *visitor, EvalContext *ctx);

const Type *evalType(EvalContext *ctx, AstNode *node);
bool evaluate(AstVisitor *visitor, AstNode *node);
void initComptime(EvalContext *ctx);

void evalStringConcatenation(EvalContext *ctx,
                             AstNode *node,
                             AstNode *lhs,
                             AstNode *rhs);
bool evalStringBuilderAppend(EvalContext *ctx,
                             StringBuilder *sb,
                             AstNode *node);

AstNode *evalAstNodeMemberAccess(EvalContext *ctx,
                                 const FileLoc *loc,
                                 AstNode *node,
                                 cstring name);

bool verifyBinaryExprOperand(EvalContext *ctx, AstNode *node);
bool evalBooleanCast(EvalContext *ctx, AstNode *node);

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

#ifdef __cplusplus
}
#endif
