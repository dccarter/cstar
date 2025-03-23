//
// Created by Carter Mbotho on 2024-12-11.
//

#include "lang/frontend/flag.h"
#include "lang/frontend/visitor.h"
#include "lang/middle/n2e.h"
#include "mem.h"

#include <lang/frontend/strings.h>
#include <lang/frontend/ttable.h>

//clang-format off
#define MEM_FINALIZE_CONTEXT_STACK                                             \
    AstModifier blockModifier;                                                 \
    AstNode *current;

//clang-format on

typedef struct MemFinalizeContext {
    Log *L;
    MemPool *pool;
    StrPool *strings;
    TypeTable *types;
    AstModifier rootModifier;
    N2eContext n2e;

    union {
        struct {
            MEM_FINALIZE_CONTEXT_STACK
        };
        struct {
            MEM_FINALIZE_CONTEXT_STACK
        } stack;
    };
} MemFinalizeContext;

static bool isNoop(const AstNode *node)
{
    if (nodeIs(node, ExprStmt))
        return isNoop(node->exprStmt.expr);
    return nodeIs(node, Noop);
}

static bool isWithinCleanupBlock(const AstNode *node)
{
    AstNode *parent = node->parentScope;
    if (!nodeIs(parent, ExprStmt))
        return false;
    parent = parent->parentScope;
    return nodeIs(parent, BasicBlock);
}

static bool hasVariableDropFlags(const AstNode *node)
{
    return (nodeIs(node, FuncParamDecl) || nodeIs(node, VarDecl)) &&
           node->varDecl.dropFlags != NULL;
}

static AstNode *makeAssignDropFlagsNode(MemFinalizeContext *ctx,
                                        AstNode *dropFlags,
                                        bool drop)
{
    return makeExprStmt(
        ctx->pool,
        builtinLoc(),
        flgNone,
        makeAssignExpr(
            ctx->pool,
            builtinLoc(),
            flgNone,
            makeResolvedIdentifier(ctx->pool,
                                   builtinLoc(),
                                   dropFlags->_name,
                                   0,
                                   dropFlags,
                                   NULL,
                                   dropFlags->type),
            opAssign,
            makeBoolLiteral(
                ctx->pool, builtinLoc(), drop, NULL, dropFlags->type),
            NULL,
            dropFlags->type),
        NULL,
        dropFlags->type);
}

static AstNode *makeZeromemAstNode(MemFinalizeContext *ctx, AstNode *operand)
{
    return makeExprStmt(
        ctx->pool,
        &operand->loc,
        flgNone,
        makeBackendCallExpr(
            ctx->pool,
            &operand->loc,
            flgNone,
            bfiZeromem,
            makePointerOfExpr(
                ctx->pool,
                &operand->loc,
                flgNone,
                operand,
                NULL,
                makePointerType(ctx->types, operand->type, flgNone)),
            makeVoidType(ctx->types)),
        NULL,
        makeVoidType(ctx->types));
}

static void visitBackendCallExpr(AstVisitor *visitor, AstNode *node)
{
    MemFinalizeContext *ctx = getAstVisitorContext(visitor);
    AstNode *args = node->backendCallExpr.args;
    csAssert0(args);
    const Type *type =
        resolveUnThisUnwrapType(stripPointerOrReference(args->type));
    AstNode *func = NULL, *var = NULL;
    switch (node->backendCallExpr.func) {
    case bfiCopy:
        if (isStructType(type))
            func = findMemberDeclInType(type, S_CopyOverload);
        else if (isUnionType(type))
            func = type->tUnion.copyFunc->func.decl;
        else if (isTupleType(type) && type->tuple.copyFunc)
            func = type->tuple.copyFunc->func.decl;
        break;
    case bfiDrop:
        if (isStructType(type))
            func = findMemberDeclInType(type, S_DestructorOverload);
        else if (isUnionType(type))
            func = type->tUnion.destructorFunc->func.decl;
        else if (isTupleType(type) && type->tuple.destructorFunc)
            func = type->tuple.destructorFunc->func.decl;
        var = resolveIdentifier(args);
        if (hasVariableDropFlags(var)) {
            node->backendCallExpr.dropFlags = var->varDecl.dropFlags;
            if (!isWithinCleanupBlock(node)) {
                astModifierAddAsNext(&ctx->blockModifier,
                                     makeAssignDropFlagsNode(
                                         ctx, var->varDecl.dropFlags, false));
            }
        }
        break;
    case bfiAlloca:
        if (isClassType(type))
            func = findMemberDeclInType(type, S_DestructorFwd);
        break;
    default:
        break;
    }

    if (func) {
        NodeToExternDecl *f2e = n2eGetNodeToExternDecl(&ctx->n2e, func);
        if (f2e == NULL) {
            // new function declaration added, add extern
            AstNode *decl = makeAstNode(ctx->pool,
                                        &func->loc,
                                        &(AstNode){.tag = astExternDecl,
                                                   .type = func->type,
                                                   .flags = func->flags,
                                                   .externDecl.func = func});

            n2eAddNodeToExternDecl(&ctx->n2e, func, decl);
            astModifierAddHead(&ctx->rootModifier, decl);
        }
    }
}

static void visitBasicBlock(AstVisitor *visitor, AstNode *node)
{
    MemFinalizeContext *ctx = getAstVisitorContext(visitor);
    AstNode *stmt = node->basicBlock.stmts.first, *parent = node->parentScope;
    node->basicBlock.stmts.first = NULL;
    for (; stmt != NULL;) {
        AstNode *tmp = stmt;
        stmt = stmt->next;
        tmp->next = NULL;

        if (isNoop(tmp))
            continue;
        astVisit(visitor, tmp);
        insertAstNode(&node->basicBlock.stmts, tmp);
    }

    if (node->basicBlock.stmts.first || node->next || !nodeIs(parent, FuncDecl))
        return;
    csAssert0(typeIs(parent->type->func.retType, Void));
    insertAstNode(&node->basicBlock.stmts,
                  makeReturnAstNode(ctx->pool,
                                    builtinLoc(),
                                    flgNone,
                                    NULL,
                                    NULL,
                                    makeVoidType(ctx->types)));
}

static void visitAssignExpr(AstVisitor *visitor, AstNode *node)
{
    MemFinalizeContext *ctx = getAstVisitorContext(visitor);
    AstNode *lhs = resolveIdentifier(node->assignExpr.lhs);
    if (hasVariableDropFlags(lhs)) {
        astModifierAddAsNext(
            &ctx->blockModifier,
            makeAssignDropFlagsNode(ctx, lhs->varDecl.dropFlags, true));
    }
    astVisitFallbackVisitAll(visitor, node);
}

static void visitUnaryExpr(AstVisitor *visitor, AstNode *node)
{
    MemFinalizeContext *ctx = getAstVisitorContext(visitor);
    AstNode *operand = node->unaryExpr.operand,
            *resolved = resolveIdentifier(operand);

    switch (node->unaryExpr.op) {
    case opMove:
        if (hasVariableDropFlags(resolved)) {
            astModifierAddAsNext(&ctx->blockModifier,
                                 makeAssignDropFlagsNode(
                                     ctx, resolved->varDecl.dropFlags, false));
        }
        else if ((nodeIs(operand, MemberExpr) || nodeIs(operand, IndexExpr)) &&
                 isDestructible(operand->type)) {
            astModifierAddAsNext(
                &ctx->blockModifier,
                makeZeromemAstNode(ctx, deepCloneAstNode(ctx->pool, operand)));
        }
        break;
    case opDelete:
        if ((nodeIs(operand, MemberExpr) || nodeIs(operand, IndexExpr)) &&
            isDestructible(operand->type)) {
            astModifierAddAsNext(
                &ctx->blockModifier,
                makeZeromemAstNode(ctx, deepCloneAstNode(ctx->pool, operand)));
        }
    default:
        break;
    }
    astVisitFallbackVisitAll(visitor, node);
}

static void visitBlockStmt(AstVisitor *visitor, AstNode *node)
{
    MemFinalizeContext *ctx = getAstVisitorContext(visitor);
    astModifierInit(&ctx->blockModifier, node);
    AstNode *stmt = node->blockStmt.stmts;
    for (; stmt != NULL; stmt = stmt->next) {
        astModifierNext(&ctx->blockModifier, stmt);
        astVisit(visitor, stmt);
    }

    stmt = node->blockStmt.stmts;
    AstNodeList stmts = {};
    for (; stmt != NULL;) {
        AstNode *tmp = stmt;
        stmt = stmt->next;
        tmp->next = NULL;
        if (isNoop(tmp))
            continue;
        insertAstNode(&stmts, tmp);
    }
    node->blockStmt.stmts = stmts.first;
}

static void visitFuncDecl(AstVisitor *visitor, AstNode *node)
{
    MemFinalizeContext *ctx = getAstVisitorContext(visitor);
    n2eAddNodeToExternDecl(&ctx->n2e, node, node);
    astVisitFallbackVisitAll(visitor, node);
}

static void visitExternDecl(AstVisitor *visitor, AstNode *node)
{
    MemFinalizeContext *ctx = getAstVisitorContext(visitor);
    AstNode *func = node->externDecl.func;
    if (!hasFlag(func, Extern))
        n2eAddNodeToExternDecl(&ctx->n2e, func, node);
}

static void visitProgram(AstVisitor *visitor, AstNode *node)
{
    MemFinalizeContext *ctx = getAstVisitorContext(visitor);
    astModifierInit(&ctx->rootModifier, node);
    AstNode *decl = node->program.decls;
    for (; decl != NULL; decl = decl->next) {
        astModifierNext(&ctx->rootModifier, decl);
        astVisit(visitor, decl);
    }
}

static void withSavedStack(Visitor func, AstVisitor *visitor, AstNode *node)
{
    MemFinalizeContext *ctx = getAstVisitorContext(visitor);
    __typeof(ctx->stack) stack = ctx->stack;
    node->parentScope = node->parentScope ?: ctx->current;
    ctx->current = node;
    func(visitor, node);

    ctx->stack = stack;
}

void memoryFinalize(CompilerDriver *cc, AstNode *node)
{
    // clang-format off
    MemFinalizeContext context = {
        .L = cc->L,
        .pool = cc->pool,
        .strings = cc->strings,
        .types = cc->types,
    };

    AstVisitor visitor = makeAstVisitor(&context, {
        [astProgram] = visitProgram,
        [astBlockStmt] = visitBlockStmt,
        [astBasicBlock] = visitBasicBlock,
        [astBackendCall] = visitBackendCallExpr,
        [astAssignExpr] = visitAssignExpr,
        [astUnaryExpr] = visitUnaryExpr,
        [astFuncDecl] = visitFuncDecl,
        [astExternDecl] = visitExternDecl,
        [astGenericDecl] = astVisitSkip,
        [astMacroDecl] = astVisitSkip
    }, .fallback = astVisitFallbackVisitAll, .dispatch = withSavedStack);
    // clang-format on

    n2eInit(&context.n2e, context.pool);
    astVisit(&visitor, node);
    n2eDeinit(&context.n2e);
}
