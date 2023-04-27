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
#include "lang/scope.h"

typedef struct {
    Log *L;
    MemPool *pool;
    StrPool *strPool;
    TypeTable *typeTable;
    HashTable builtinMacros;
    Env env;
    Env exports;
    AstNode *previousTopLevelDecl;
    AstNode *currentTopLevelDecl;
    AstNode *program;
    u64 anonymousDeclsIndex;
    bool mainOptimized : 1;
    union {
        Scope *closure;
        AstNode *current;
        const AstNode *lastReturn;
        struct {
            Scope *closure;
            AstNode *current;
            const AstNode *lastReturn;
        } stack;
    };
} SemanticsContext;

typedef AstNode *(*EvaluateMacro)(AstVisitor *, const AstNode *, AstNode *);

#define ERROR_TYPE(CTX) makeErrorType((CTX)->typeTable)

void semanticsCheck(AstNode *program,
                    Log *L,
                    MemPool *pool,
                    StrPool *strPool,
                    TypeTable *typeTable);

const Type *evalType(AstVisitor *visitor, AstNode *node);
void addModuleExport(SemanticsContext *ctx, AstNode *node, cstring name);
void defineDeclarationAliasName(SemanticsContext *ctx, AstNode *node);

void initializeModule(AstVisitor *visitor, AstNode *node);
void finalizeModule(AstVisitor *visitor, AstNode *node, cstring namespace);

AstNode *makeTypeReferenceNode(SemanticsContext *ctx, const Type *type);
u64 checkMany(AstVisitor *visitor, AstNode *node);
void addTopLevelDecl(SemanticsContext *ctx, cstring name, AstNode *node);
void transformToMemberCallExpr(AstVisitor *visitor,
                               AstNode *node,
                               AstNode *func,
                               AstNode *target,
                               cstring member,
                               AstNode *args);
void initializeBuiltins(SemanticsContext *ctx);
const Type *checkBuiltinTypeProperty(SemanticsContext *ctx,
                                     AstNode *target,
                                     cstring name);

AstNode *findSymbolByPath(SemanticsContext *ctx,
                          const Env *env,
                          const AstNode *node);
AstNode *findSymbolByNode(SemanticsContext *ctx,
                          const Env *env,
                          const AstNode *node);
AstNode *findSymbolOnlyByNode(const Env *env, const AstNode *node);

AstNode *checkGenericDeclReference(AstVisitor *visitor,
                                   AstNode *node,
                                   AstNode *path,
                                   const Env *env);
void checkGenericParam(AstVisitor *visitor, AstNode *node);
void checkGenericDecl(AstVisitor *visitor, AstNode *node);
void checkPathElement(AstVisitor *visitor, AstNode *node);
void checkPath(AstVisitor *visitor, AstNode *node);
void checkBlock(AstVisitor *visitor, AstNode *node);

void checkLiterals(AstVisitor *visitor, AstNode *node);
void checkStringExpr(AstVisitor *visitor, AstNode *node);
void checkForStmt(AstVisitor *visitor, AstNode *node);
void checkReturnStmt(AstVisitor *visitor, AstNode *node);
void checkIfStmt(AstVisitor *visitor, AstNode *node);
void checkWhileStmt(AstVisitor *visitor, AstNode *node);
void checkSwitchStmt(AstVisitor *visitor, AstNode *node);
void checkCaseStmt(AstVisitor *visitor, AstNode *node);
void checkMember(AstVisitor *visitor, AstNode *node);
void checkClosure(AstVisitor *visitor, AstNode *node);
void checkMethodDeclBody(AstVisitor *visitor, AstNode *node);
const Type *checkMethodDeclSignature(AstVisitor *visitor, AstNode *node);
void checkFuncParam(AstVisitor *visitor, AstNode *node);
void checkFunctionDecl(AstVisitor *visitor, AstNode *node);
void checkCall(AstVisitor *visitor, AstNode *node);
void checkRangeExpr(AstVisitor *visitor, AstNode *node);
void checkNewExpr(AstVisitor *visitor, AstNode *node);
void checkTupleExpr(AstVisitor *visitor, AstNode *node);
void checkStructExpr(AstVisitor *visitor, AstNode *node);
void checkArrayExpr(AstVisitor *visitor, AstNode *node);
void checkIndex(AstVisitor *visitor, AstNode *node);
void checkBinaryExpr(AstVisitor *visitor, AstNode *node);
void checkUnaryExpr(AstVisitor *visitor, AstNode *node);
void checkAddressOfExpr(AstVisitor *visitor, AstNode *node);
void checkAssignExpr(AstVisitor *visitor, AstNode *node);
void checkTernaryExpr(AstVisitor *visitor, AstNode *node);
void checkMacroExpr(AstVisitor *visitor, AstNode *node);
void checkVarDecl(AstVisitor *visitor, AstNode *node);
void checkTypeDecl(AstVisitor *visitor, AstNode *node);
void checkUnionDecl(AstVisitor *visitor, AstNode *node);
void checkStructField(AstVisitor *visitor, AstNode *node);
void checkStructDecl(AstVisitor *visitor, AstNode *node);
void checkEnumDecl(AstVisitor *visitor, AstNode *node);
void checkImportDecl(AstVisitor *visitor, AstNode *node);
void checkFuncType(AstVisitor *visitor, AstNode *node);
void checkTupleType(AstVisitor *visitor, AstNode *node);
void checkBuiltinType(AstVisitor *visitor, AstNode *node);
void checkOptionalType(AstVisitor *visitor, AstNode *node);
void checkPrimitiveType(AstVisitor *visitor, AstNode *node);
void checkPointerType(AstVisitor *visitor, AstNode *node);
void checkArrayType(AstVisitor *visitor, AstNode *node);