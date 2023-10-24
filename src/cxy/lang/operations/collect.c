//
// Created by Carter Mbotho on 2023-10-01.
//

#include "lang/operations.h"

#include "lang/ast.h"
#include "lang/flag.h"
#include "lang/strings.h"
#include "lang/visitor.h"

#include "core/alloc.h"

typedef struct {
    Log *L;
    MemPool *pool;
    StrPool *strings;
    bool collectTree;
} CollectAstContext;

static void returnAstNodeMemory(AstVisitor *visitor, AstNode *node)
{
    CollectAstContext *ctx = getAstVisitorContext(visitor);
    if (node->attrs) {
        astVisitManyNodes(visitor, node->attrs);
    }
    freeMemToPoolCache(ctx->pool, memAstNode, node, sizeof(*node));
}

static void collectFunctionDecl(AstVisitor *visitor, AstNode *node)
{
    CollectAstContext *ctx = getAstVisitorContext(visitor);
    if (node->funcDecl.body) {
        ctx->collectTree = true;
        astVisit(visitor, node->funcDecl.body);
        ctx->collectTree = false;
        node->funcDecl.body = NULL;
    }
}

static void collectSkip(AstVisitor *visitor, AstNode *node) {}

static void collectDispatch(Visitor func, AstVisitor *visitor, AstNode *node)
{
    CollectAstContext *ctx = getAstVisitorContext(visitor);
    func(visitor, node);
    if (ctx->collectTree)
        returnAstNodeMemory(visitor, node);
}

AstNode *collectAst(CompilerDriver *driver, AstNode *node)
{
    CollectAstContext context = {.L = driver->L,
                                 .pool = &driver->pool,
                                 .strings = &driver->strPool,
                                 .collectTree = false};

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context, {
        [astGenericDecl] = collectSkip,
        [astFuncDecl] = collectFunctionDecl
    }, .fallback = astVisitFallbackVisitAll, .dispatch = collectDispatch);
    // clang-format on

    astVisit(&visitor, node);

    return node;
}
