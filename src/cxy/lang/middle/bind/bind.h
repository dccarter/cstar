//
// Created by Carter Mbotho on 2023-11-03.
//

#pragma once

#include "lang/operations.h"

#include "lang/frontend/visitor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Log *L;
    MemPool *pool;
    StrPool *strings;
    Env *env;
    AstModifier root;
    union {
        struct {
            bool isComptimeContext;
            bool inTestCase;
            bool isConstFunc;
            AstNode *currentClosure;
        };
        struct {
            bool isComptimeContext;
            bool inTestCase;
            bool isConstFunc;
            AstNode *currentClosure;
        } stack;
    };
} BindContext;

void defineDeclaration(BindContext *ctx, cstring name, AstNode *node);

AstNode *bindAstPhase1(CompilerDriver *driver, Env *env, AstNode *node);
void bindAstPhase2(CompilerDriver *driver, Env *env, AstNode *node);

#ifdef __cplusplus
}
#endif
