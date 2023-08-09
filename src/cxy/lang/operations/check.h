//
// Created by Carter Mbotho on 2023-07-26.
//

#pragma once

#include <lang/ast.h>
#include <lang/visitor.h>

#include <core/strpool.h>

#define ERROR_TYPE(CTX) makeErrorType((CTX)->types)

typedef struct {
    Log *L;
    MemPool *pool;
    StrPool *strings;
    TypeTable *types;
    struct {
        AstNode *program;
        AstNode *previous;
        AstNode *current;
    } root;

    struct {
        AstNode *self;
        AstNode *previous;
        AstNode *current;
    } block;

    union {
        struct {
            AstNode *currentCall;
            AstNode *currentStruct;
        } stack;

        struct {
            AstNode *currentCall;
            AstNode *currentStruct;
        };
    };
} TypingContext;

void transformToMemberCallExpr(AstVisitor *visitor,
                               AstNode *node,
                               AstNode *target,
                               cstring member,
                               AstNode *args);

const Type *matchOverloadedFunction(TypingContext *ctx,
                                    const Type *callee,
                                    const Type **argTypes,
                                    u64 argsCount,
                                    const FileLoc *loc,
                                    u64 flags);

const Type *checkPathElement(AstVisitor *visitor,
                             const Type *parent,
                             AstNode *node);

const Type *checkType(AstVisitor *visitor, AstNode *node);
const Type *checkFunctionSignature(AstVisitor *visitor, AstNode *node);
const Type *checkFunctionBody(AstVisitor *visitor, AstNode *node);

void checkStructField(AstVisitor *visitor, AstNode *node);
void checkStructDecl(AstVisitor *visitor, AstNode *node);
void checkBinaryExpr(AstVisitor *visitor, AstNode *node);
