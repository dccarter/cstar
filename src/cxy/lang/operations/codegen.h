//
// Created by Carter on 2023-08-30.
//

#pragma once

#include "core/strpool.h"
#include "lang/visitor.h"

typedef struct {
    FormatState *state;
    TypeTable *types;
    StrPool *strPool;
    AstNode *program;
    cstring namespace;
    bool importedFile;
} CodegenContext;

void writeNamespace(CodegenContext *ctx, cstring sep);
void writeDeclNamespace(CodegenContext *ctx, cstring namespace, cstring sep);
void writeEnumPrefix(CodegenContext *ctx, const Type *type);
void generateTypeUsage(CodegenContext *ctx, const Type *type);
void writeTypename(CodegenContext *ctx, const Type *type);
cstring getNativeDeclarationAliasName(const AstNode *node);

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

void generateStructDelete(CodegenContext *context, const Type *type);

void generateArrayToSlice(ConstAstVisitor *visitor,
                          const Type *slice,
                          const AstNode *value);

void generateTypeinfo(ConstAstVisitor *visitor, const AstNode *node);
void generateFuncParam(ConstAstVisitor *visitor, const AstNode *node);
void generatePath(ConstAstVisitor *visitor, const AstNode *node);
void generatePathElement(ConstAstVisitor *visitor, const AstNode *node);
void generateBlock(ConstAstVisitor *visitor, const AstNode *node);

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
void generateForStmt(ConstAstVisitor *visitor, const AstNode *node);
void generateWhileStmt(ConstAstVisitor *visitor, const AstNode *node);
void generateSwitchStmt(ConstAstVisitor *visitor, const AstNode *node);
void generateCaseStmt(ConstAstVisitor *visitor, const AstNode *node);
void generateIfStmt(ConstAstVisitor *visitor, const AstNode *node);
void generateReturnStmt(ConstAstVisitor *visitor, const AstNode *node);
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
void generateAddressOfExpr(ConstAstVisitor *visitor, const AstNode *node);
void generateTernaryExpr(ConstAstVisitor *visitor, const AstNode *node);
void generateAssignExpr(ConstAstVisitor *visitor, const AstNode *node);
void generateVariableDecl(ConstAstVisitor *visitor, const AstNode *node);
void generateTypeDecl(ConstAstVisitor *visitor, const AstNode *node);
void generateStructDecl(ConstAstVisitor *visitor, const AstNode *node);
