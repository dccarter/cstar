//
// Created by Carter on 2023-09-14.
//
#include "lang/operations.h"

#include "lang/ast.h"
#include "lang/flag.h"
#include "lang/strings.h"
#include "lang/visitor.h"

#include "core/alloc.h"

typedef struct {
    Log *L;
} FinalizeContext;

static bool isUnSupportedByCodegen(AstNode *node)
{
    return nodeIs(node, Nop) || hasFlag(node, Comptime);
}

static void finalizeProgram(AstVisitor *visitor, AstNode *node)
{
    astVisit(visitor, node->program.top);

    AstNode *decl = node->program.decls, *prev = NULL;
    for (; decl; decl = decl->next) {
        astVisit(visitor, decl);
        if (isUnSupportedByCodegen(decl)) {
            // unlink all unsupported nodes
            if (prev == NULL) {
                node->program.decls = decl->next;
            }
            else
                prev->next = decl->next;
            continue;
        }
        prev = decl;
    }
}

static void finalizeBlock(AstVisitor *visitor, AstNode *node)
{
    AstNode *stmt = node->blockStmt.stmts, *prev = NULL;
    for (; stmt; stmt = stmt->next) {
        astVisit(visitor, stmt);
        if (isUnSupportedByCodegen(stmt))
            stmt->flags |= flgCodeGenerated;
    }
}

static void finalizeStmtExpr(AstVisitor *visitor, AstNode *node)
{
    AstNode *stmt = node->stmtExpr.stmt;
    astVisit(visitor, stmt);
    if (isUnSupportedByCodegen(stmt))
        node->flags |= flgCodeGenerated;
}

static void finalizeExprStmt(AstVisitor *visitor, AstNode *node)
{
    AstNode *expr = node->exprStmt.expr;
    astVisit(visitor, expr);
    if (isUnSupportedByCodegen(expr))
        node->flags |= flgCodeGenerated;
}

static void finalizePath(AstVisitor *visitor, AstNode *node)
{
    AstNode *base = node->path.elements,
            *resolved = base->pathElement.resolvesTo;
    if (nodeIs(resolved, Identifier) && hasFlag(resolved, Define)) {
        if (resolved->ident.alias) {
            base->pathElement.name = resolved->ident.value;
        }
    }
}

static void finalizeNoop(AstVisitor *visitor, AstNode *node) {}

AstNode *finalizeAst(CompilerDriver *driver, AstNode *node)
{
    FinalizeContext context = {.L = driver->L};

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context, {
        [astProgram] = finalizeProgram,
        [astStmtExpr] = finalizeStmtExpr,
        [astExprStmt] = finalizeExprStmt,
        [astBlockStmt] = finalizeBlock,
        [astGenericDecl] = finalizeNoop,
        [astPath] = finalizePath
    }, .fallback = astVisitFallbackVisitAll);
    // clang-format on

    astVisit(&visitor, node);

    return node;
}