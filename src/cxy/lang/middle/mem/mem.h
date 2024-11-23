//
// Created by Carter Mbotho on 2024-04-21.
//

#pragma once

#include <core/strpool.h>
#include <lang/frontend/visitor.h>
#include <lang/middle/n2e.h>
#include <lang/middle/scope.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VariableTrace {
    AstNode *variable;
    const BlockScope *scope;
    u8 state;
    u32 prev;
} VariableTrace;

enum { vtsUnsigned = 0, vtsAssigned = 1, vtsMaybeAssigned = 2 };

typedef struct MemoryManagementContext {
    Log *L;
    MemPool *pool;
    StrPool *strings;
    TypeTable *types;
    AstNode *program;
    BlockScopeContainer bsc;
    N2eContext n2e;
    bool traceMemory;
    bool localVarsInReturn;
    AstModifier root;
    union {
        struct {
            AstModifier block;
            AstNode *current;
            AstNode *func;
            bool inConditionalBlock;
        };
        struct {
            AstModifier block;
            AstNode *current;
            AstNode *func;
            bool inConditionalBlock;
        } stack;
    };
} MMContext;

void checkReferenceVariables(MMContext *context, AstNode *node);
void manageMemory(MMContext *context, AstNode *node);
void withSavedStack(Visitor func, AstVisitor *visitor, AstNode *node);

const AstNode *resolveCallExpr(const AstNode *node);
const AstNode *getCallExprCalleeFunc(const AstNode *node);
bool isTransientCallExpr(const AstNode *node);

BlockScope *vtPushBlockScope(BlockScopeContainer *bsc, AstNode *node);
void vtPopBlockScope(BlockScopeContainer *bsc);
u32 vtCreate(BlockScope *scope, AstNode *var);
VariableTrace *vtGet(BlockScope *scope, u32 idx);

#ifdef __cplusplus
}
#endif
