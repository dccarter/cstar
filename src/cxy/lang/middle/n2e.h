//
// Created by Carter Mbotho on 2024-07-25.
//

#pragma once

#include <lang/frontend/ast.h>

typedef struct {
    AstNode *node;
    AstNode *target;
} NodeToExternDecl;

typedef struct {
    HashTable n2e;
    MemPool *pool;
} N2eContext;

void n2eInit(N2eContext *ctx, MemPool *pool);
void n2eDeinit(N2eContext *ctx);

void n2eAddNodeToExternDecl(N2eContext *ctx, AstNode *node, AstNode *target);
NodeToExternDecl *n2eGetNodeToExternDecl(N2eContext *ctx, AstNode *decl);
AstNode *n2eMakeExternReferenceToBuiltin(N2eContext *ctx, cstring builtin);
