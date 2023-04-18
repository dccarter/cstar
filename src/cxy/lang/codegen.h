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

void generateFallback(ConstAstVisitor *visitor, const AstNode *node);
void generateForStmt(ConstAstVisitor *visitor, const AstNode *node);
void generateLiteral(ConstAstVisitor *visitor, const AstNode *node);
void generateStringExpr(ConstAstVisitor *visitor, const AstNode *node);
void generateMemberExpr(ConstAstVisitor *visitor, const AstNode *node);
void generateClosure(ConstAstVisitor *visitor, const AstNode *node);