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

#include "lang/middle/mangle.h"
#include "lang/middle/scope.h"

#define bscSealed BIT(1)

typedef struct DeferContext {
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
} DeferContext;

static inline bool blockScopeIsSealed(BlockScope *scope)
{
    return (scope->flags & bscSealed) == bscSealed;
}

static bool astNodeNameNeedsMangling(const AstNode *node)
{
    return nodeIs(node, FuncDecl) &&
           (node->list.link != NULL || node->funcDecl.index > 0);
}

static inline bool blockScopeNeedsSealing(BlockScope *scope)
{
    return !blockScopeIsSealed(scope) && scope->data.size > 0;
}

static void blockScopeSealIntoList(DeferContext *ctx,
                                   AstNodeList *list,
                                   BlockScope *scope)
{
    for (i64 i = scope->data.size - 1; i >= 0; i--) {
        AstNode *expr = dynArrayAt(AstNode **, &scope->data, i);
        expr = deepCloneAstNode(ctx->pool, expr);
        if (!nodeIs(expr, ExprStmt))
            expr = makeExprStmt(ctx->pool,
                                &expr->loc,
                                flgNone,
                                expr,
                                NULL,
                                makeVoidType(ctx->types));
        insertAstNode(list, expr);
    }
}

static void blockScopeSeal(DeferContext *ctx, BlockScope *scope)
{
    AstNodeList expressions = {};
    blockScopeSealIntoList(ctx, &expressions, scope);
    astModifierAdd(&ctx->block, expressions.first);
    scope->flags |= bscSealed;
}

static void visitDeferStmt(AstVisitor *visitor, AstNode *node)
{
    DeferContext *ctx = getAstVisitorContext(visitor);
    BlockScope *scope = ctx->bsc.scope;
    pushOnDynArray(&scope->data, &node->deferStmt.stmt);
    node->tag = astNoop;
    clearAstBody(node);
}

static void visitBlockStmt(AstVisitor *visitor, AstNode *node)
{
    DeferContext *ctx = getAstVisitorContext(visitor);
    BlockScope *scope = blockScopeContainerPush(&ctx->bsc, node, flgNone);

    AstNode *stmt = node->blockStmt.stmts;
    astModifierInit(&ctx->block, node);
    AstNodeList stmts = {};

    for (; stmt && !blockScopeIsSealed(scope); stmt = stmt->next) {
        astModifierNext(&ctx->block, stmt);
        astVisit(visitor, stmt);
    }

    if (blockScopeNeedsSealing(scope)) {
        astModifierNext(&ctx->block, stmt);
        blockScopeSeal(ctx, scope);
    }

    blockScopeContainerPop(&ctx->bsc);

    for (stmt = node->blockStmt.stmts; stmt;) {
        AstNode *curr = stmt;
        stmt = stmt->next;
        curr->next = NULL;
        if (!nodeIs(curr, Noop)) {
            insertAstNode(&stmts, curr);
        }
    }
    node->blockStmt.stmts = stmts.first;
}

static void visitBreakContinue(AstVisitor *visitor, AstNode *node)
{
    DeferContext *ctx = getAstVisitorContext(visitor);
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
    if (scope)
        blockScopeSealIntoList(ctx, &expressions, scope);

    ctx->bsc.scope->flags |= bscSealed;
    astModifierAdd(&ctx->block, expressions.first);
}

static void visitReturnStmt(AstVisitor *visitor, AstNode *node)
{
    DeferContext *ctx = getAstVisitorContext(visitor);
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

static void visitFuncDecl(AstVisitor *visitor, AstNode *node)
{
    DeferContext *ctx = getAstVisitorContext(visitor);
    if (astNodeNameNeedsMangling(node)) {
        node->funcDecl.name = makeMangledName(ctx->strings,
                                              node->funcDecl.name,
                                              node->type->func.params,
                                              node->type->func.paramsCount,
                                              hasFlag(node, Const));
    }
    astVisit(visitor, node->funcDecl.body);
}

static void withSavedStack(Visitor func, AstVisitor *visitor, AstNode *node)
{
    DeferContext *ctx = getAstVisitorContext(visitor);
    __typeof(ctx->stack) stack = ctx->stack;

    func(visitor, node);

    ctx->stack = stack;
}

AstNode *simplifyDeferStatements(CompilerDriver *driver, AstNode *node)
{
    DeferContext context = {.L = driver->L,
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
        [astFuncDecl] = visitFuncDecl,
        [astGenericDecl] = astVisitSkip,
        [astMacroDecl] = astVisitSkip
    }, .fallback = astVisitFallbackVisitAll, .dispatch = withSavedStack);
    // clang-format on

    blockScopeContainerInit(&context.bsc, sizeof(AstNode *));
    astVisit(&visitor, node);
    blockScopeContainerDeinit(&context.bsc);

    return node;
}
