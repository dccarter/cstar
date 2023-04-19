/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-17
 */

#pragma once

#include "core/strpool.h"
#include "lang/ast.h"

typedef struct {
    FormatState *state;
    TypeTable *types;
    StrPool *strPool;
    cstring namespace;
} CodegenContext;

void writeNamespace(CodegenContext *ctx, cstring sep);
void writeEnumPrefix(CodegenContext *ctx, const Type *type);
void generateTypeUsage(CodegenContext *ctx, const Type *type);
void writeTypename(CodegenContext *ctx, const Type *type);

void generateManyAstsWithinBlock(ConstAstVisitor *visitor,
                                 const char *sep,
                                 const AstNode *nodes,
                                 bool newLine);

void generateManyAsts(ConstAstVisitor *visitor,
                      const char *sep,
                      const AstNode *nodes);

void generateAstWithDelim(ConstAstVisitor *visitor,
                          const char *open,
                          const char *close,
                          const AstNode *node);

void generateManyAstsWithDelim(ConstAstVisitor *visitor,
                               const char *open,
                               const char *sep,
                               const char *close,
                               const AstNode *nodes);

void generateCode(FormatState *state,
                  TypeTable *table,
                  StrPool *strPool,
                  const AstNode *prog);

void codegenPrologue(CodegenContext *context, const AstNode *prog);
void codegenEpilogue(CodegenContext *context, const AstNode *prog);

void generateFuncParam(ConstAstVisitor *visitor, const AstNode *node);
void generatePath(ConstAstVisitor *visitor, const AstNode *node);
void generatePathElement(ConstAstVisitor *visitor, const AstNode *node);

void generateTupleDefinition(CodegenContext *context, const Type *type);
void generateStructDefinition(CodegenContext *context, const Type *type);
void generateEnumDefinition(CodegenContext *context, const Type *type);
void generateFunctionDefinition(ConstAstVisitor *visitor, const AstNode *node);
void generateFuncDeclaration(CodegenContext *context, const Type *type);
void generateArrayDeclaration(CodegenContext *context, const Type *type);
void generateFunctionTypedef(CodegenContext *context, const Type *type);
void generateStructTypedef(CodegenContext *ctx, const Type *type);

void generateFallback(ConstAstVisitor *visitor, const AstNode *node);
void generateArrayExpr(ConstAstVisitor *visitor, const AstNode *node);
void generateForStmtArray(ConstAstVisitor *visitor, const AstNode *node);
void generateForStmtRange(ConstAstVisitor *visitor, const AstNode *node);
void generateForStmt(ConstAstVisitor *visitor, const AstNode *node);
void generateLiteral(ConstAstVisitor *visitor, const AstNode *node);
void generateStringExpr(ConstAstVisitor *visitor, const AstNode *node);
void generateClosureExpr(ConstAstVisitor *visitor, const AstNode *node);
void generateMemberExpr(ConstAstVisitor *visitor, const AstNode *node);
void generateCallExpr(ConstAstVisitor *visitor, const AstNode *node);
void generateNewExpr(ConstAstVisitor *visitor, const AstNode *node);
void generateTupleExpr(ConstAstVisitor *visitor, const AstNode *node);
void generateStructExpr(ConstAstVisitor *visitor, const AstNode *node);
void generateIndexExpr(ConstAstVisitor *visitor, const AstNode *node);
void generateBinaryExpr(ConstAstVisitor *visitor, const AstNode *node);
void generateUnaryExpr(ConstAstVisitor *visitor, const AstNode *node);
void generateVariableDecl(ConstAstVisitor *visitor, const AstNode *node);
void generateTypeDecl(ConstAstVisitor *visitor, const AstNode *node);