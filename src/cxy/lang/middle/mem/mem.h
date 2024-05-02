//
// Created by Carter Mbotho on 2024-04-21.
//

#pragma once

#include <core/strpool.h>
#include <lang/frontend/visitor.h>
#include <lang/middle/scope.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VariableTrace {
    AstNode *variable;
    const BlockScope *scope;
} VariableTrace;

typedef struct MemoryManagementContext {
    Log *L;
    MemPool *pool;
    StrPool *strings;
    TypeTable *types;
    AstNode *program;
    BlockScopeContainer bsc;
    HashTable allVariables;
    union {
        struct {
            AstModifier block;
            AstNode *current;
            bool inConditionalBlock;
        };
        struct {
            AstModifier block;
            AstNode *current;
            bool inConditionalBlock;
        } stack;
    };
} MMContext;

#ifdef __cplusplus
}
#endif
