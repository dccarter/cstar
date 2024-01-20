//
// Created by Carter Mbotho on 2023-11-03.
//

#pragma once

#include "lang/operations.h"

typedef struct {
    Log *L;
    MemPool *pool;
    StrPool *strings;
    Env *env;
    union {
        struct {
            bool isComptimeContext;
            AstNode *currentClosure;
        };
        struct {
            bool isComptimeContext;
            AstNode *currentClosure;
        } stack;
    };
} BindContext;

void defineDeclaration(BindContext *ctx, cstring name, AstNode *node);

AstNode *bindAstPhase1(CompilerDriver *driver, Env *env, AstNode *node);
void bindAstPhase2(CompilerDriver *driver, Env *env, AstNode *node);
