//
// Created by Carter Mbotho on 2024-07-25.
//

#include "n2e.h"

#include "lang/frontend/flag.h"
#include "lang/middle/builtins.h"

static bool compareNodeToExternDecl(const void *lhs, const void *rhs)
{
    return ((NodeToExternDecl *)lhs)->node == ((NodeToExternDecl *)rhs)->node;
}

void n2eInit(N2eContext *ctx, MemPool *pool)
{
    ctx->n2e = newHashTable(sizeof(NodeToExternDecl));
    ctx->pool = pool;
}

void n2eDeinit(N2eContext *ctx) { freeHashTable(&ctx->n2e); }

void n2eAddNodeToExternDecl(N2eContext *ctx, AstNode *node, AstNode *target)
{
    if (nodeIs(node, FuncDecl) ||
        (hasFlag(node, TopLevelDecl) && nodeIs(node, VarDecl))) //
    {
        insertInHashTable(
            &ctx->n2e,
            &(NodeToExternDecl){.node = node, .target = target ?: node},
            hashPtr(hashInit(), node),
            sizeof(NodeToExternDecl),
            compareNodeToExternDecl);
    }
}

NodeToExternDecl *n2eGetNodeToExternDecl(N2eContext *ctx, AstNode *decl)
{
    return findInHashTable(&ctx->n2e,
                           &(NodeToExternDecl){.node = decl, .target = decl},
                           hashPtr(hashInit(), decl),
                           sizeof(NodeToExternDecl),
                           compareNodeToExternDecl);
}

AstNode *n2eMakeExternReferenceToBuiltin(N2eContext *ctx, cstring builtin)
{

    AstNode *target = findBuiltinDecl(builtin);
    csAssert0(target);

    AstNode *decl = makeAstNode(ctx->pool,
                                &target->loc,
                                &(AstNode){.tag = astExternDecl,
                                           .type = target->type,
                                           .flags = target->flags,
                                           .externDecl.func = target});
    n2eAddNodeToExternDecl(ctx, target, decl);
    return decl;
}