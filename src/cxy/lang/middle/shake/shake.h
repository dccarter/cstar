//
// Created by Carter on 2023-09-22.
//

#ifndef CXY_SHAKE_H
#define CXY_SHAKE_H

#include "lang/frontend/ast.h"

typedef struct {
    Log *L;
    MemPool *pool;
    struct StrPool *strPool;
    struct {
        AstNode *current;
        AstNode *previous;
        AstNode *self;
    } block;
    union {
        struct {
            bool inClassOrStruct;
        };
        struct {
            bool inClassOrStruct;
        } stack;
    };
} ShakeAstContext;

AstNode *createClassOrStructBuiltins(MemPool *pool, AstNode *node);

void shakeClosureExpr(struct AstVisitor *visitor, AstNode *node);

#endif // CXY_SHAKE_H
