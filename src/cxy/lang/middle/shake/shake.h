//
// Created by Carter on 2023-09-22.
//

#pragma once

#include "lang/frontend/ast.h"
#include "lang/frontend/visitor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Log *L;
    MemPool *pool;
    struct StrPool *strings;
    AstNodeList testCases;
    AstModifier block;
    AstModifier root;
    u64 lastTestId;
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

#ifdef __cplusplus
}
#endif
