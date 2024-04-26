//
// Created by Carter on 2023-09-22.
//

#ifndef CXY_SHAKE_H
#define CXY_SHAKE_H

#include "lang/frontend/ast.h"
#include "lang/frontend/visitor.h"

typedef struct {
    Log *L;
    MemPool *pool;
    struct StrPool *strings;

    union {
        struct {
            AstModifier block;
            bool inClassOrStruct;
        };
        struct {
            AstModifier block;
            bool inClassOrStruct;
        } stack;
    };
} ShakeAstContext;

AstNode *createClassOrStructBuiltins(MemPool *pool, AstNode *node);

void shakeClosureExpr(struct AstVisitor *visitor, AstNode *node);

#endif // CXY_SHAKE_H
