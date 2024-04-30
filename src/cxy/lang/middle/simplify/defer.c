//
// Created by Carter Mbotho on 2024-04-29.
//

#include "driver/driver.h"

#include "lang/frontend/ast.h"
#include "lang/frontend/capture.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/visitor.h"

#include "lang/middle/scope.h"

#define bscSealed BIT(1)

typedef struct SimplifyDeferContext {
    Log *L;
    TypeTable *types;
    StrPool *strings;
    MemPool *pool;
    AstModifier root;
    BlockScopeContainer bsc;

    union {
        struct {
            AstModifier block;
        };
        struct {
            AstModifier block;
        } stack;
    };
} SimplifyDeferContext;

static inline bool blockScopeIsSealed(BlockScope *scope)
{
    return (scope->flags & bscSealed) == bscSealed;
}

static inline bool blockScopeNeedsSealing(BlockScope *scope)
{
    return !blockScopeIsSealed(scope) && scope->data.size > 0;
}

static void blockScopeSealIntoList(SimplifyDeferContext *ctx,
                                   AstNodeList *list,
                                   BlockScope *scope)
{
    for (i64 i = scope->data.size; i >= 0; i--) {
        AstNode *expr = dynArrayAt(AstNode **, &scope->data, i);
        insertAstNode(list, deepCloneAstNode(ctx->pool, expr));
    }
}

static void blockScopeSeal(SimplifyDeferContext *ctx, BlockScope *scope)
{
    AstNodeList expressions = {};
    blockScopeSealIntoList(ctx, &expressions, scope);
    astModifierAdd(&ctx->block, expressions.first);
    scope->flags |= bscSealed;
}

static void visitDeferStmt(AstVisitor *visitor, AstNode *node)
{
    SimplifyDeferContext *ctx = getAstVisitorContext(visitor);
    BlockScope *scope = ctx->bsc.scope;
    pushOnDynArray(&scope->data, &node->deferStmt.expr);
    node->tag = astNoop;
    clearAstBody(node);
}

static void visitBlockStmt(AstVisitor *visitor, AstNode *node)
{
    SimplifyDeferContext *ctx = getAstVisitorContext(visitor);
    BlockScope *scope = blockScopeContainerPush(&ctx->bsc, node, flgNone);

    AstNode *stmt = node->blockStmt.stmts;
    astModifierInit(&ctx->block, node);

    for (; stmt && !blockScopeIsSealed(scope); stmt = stmt->next) {
        astModifierNext(&ctx->block, stmt);
        astVisit(visitor, stmt);
    }

    if (blockScopeNeedsSealing(scope)) {
        astModifierNext(&ctx->block, stmt);
        blockScopeSeal(ctx, scope);
    }

    blockScopeContainerPop(&ctx->bsc);
}

static void visitBreakContinue(AstVisitor *visitor, AstNode *node)
{
    SimplifyDeferContext *ctx = getAstVisitorContext(visitor);
    BlockScope *scope = ctx->bsc.scope;
    AstNodeList expressions = {};
    while (scope) {
        AstNode *block = scope->node->parentScope;
        if (nodeIs(block, WhileStmt) || nodeIs(block, ForStmt))
            break;
        if (blockScopeNeedsSealing(scope))
            blockScopeSealIntoList(ctx, &expressions, scope);
        scope = scope->next;
    }
    blockScopeSealIntoList(ctx, &expressions, scope);

    ctx->bsc.scope->flags |= bscSealed;
    astModifierAdd(&ctx->block, expressions.first);
}

static void visitReturnStmt(AstVisitor *visitor, AstNode *node)
{
    SimplifyDeferContext *ctx = getAstVisitorContext(visitor);
    BlockScope *scope = ctx->bsc.scope;
    AstNodeList expressions = {};
    while (scope) {
        if (blockScopeNeedsSealing(scope))
            blockScopeSealIntoList(ctx, &expressions, scope);
        scope = scope->next;
    }
    ctx->bsc.scope->flags |= bscSealed;
    astModifierAdd(&ctx->block, expressions.first);
}

static void withSavedStack(Visitor func, AstVisitor *visitor, AstNode *node)
{
    SimplifyDeferContext *ctx = getAstVisitorContext(visitor);
    __typeof(ctx->stack) stack = ctx->stack;

    func(visitor, node);

    ctx->stack = stack;
}

AstNode *simplifyDeferStatements(CompilerDriver *driver, AstNode *node)
{
    SimplifyDeferContext context = {.L = driver->L,
                                    .types = driver->types,
                                    .strings = driver->strings,
                                    .pool = driver->pool};

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context, {
        [astBreakStmt] = visitBreakContinue,
        [astContinueStmt] = visitBreakContinue,
        [astBlockStmt] = visitBlockStmt,
        [astReturnStmt] = visitReturnStmt,
        [astDeferStmt] = visitDeferStmt,
        [astGenericDecl] = astVisitSkip,
        [astMacroDecl] = astVisitSkip
    }, .fallback = astVisitFallbackVisitAll, .dispatch = withSavedStack);
    // clang-format on

    blockScopeContainerInit(&context.bsc, sizeof(AstNode *));
    astVisit(&visitor, node);
    blockScopeContainerDeinit(&context.bsc);

    return node;
}
