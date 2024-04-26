//
// Created by Carter Mbotho on 2024-04-21.
//

#pragma once

#include <core/strpool.h>
#include <lang/frontend/visitor.h>

typedef struct BlockScope {
    struct BlockScope *next;
    DynArray variables;
    bool isConditionalBlock;
} BlockScope;

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
    BlockScope *scope;
    BlockScope *scopeCache;
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
