//
// Created by Carter Mbotho on 2024-07-26.
//

#include "driver/driver.h"
#include "lang/frontend/ast.h"
#include "lang/frontend/capture.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/visitor.h"

typedef struct SimplifyContext {
    Log *L;
    TypeTable *types;
    StrPool *strings;
    MemPool *pool;
    AstNodeList bbs;
    AstNode *currentBB;
    AstNode *funcEndBB;
    AstNode *returnVar;
    bool unreachable;
    u32 bbIndex;
    union {
        struct {
            AstNode *currentFunction;
            AstNode *condBB;
            AstNode *updateBB;
            AstNode *endBB;
        };
        struct {
            AstNode *currentFunction;
            AstNode *condBB;
            AstNode *updateBB;
            AstNode *endBB;
        } stack;
    };
} LoweringContext;

static inline bool nodeIsTerminator(const AstNode *node)
{
    switch (node->tag) {
    case astBranchIf:
    case astBranch:
    case astReturnStmt:
        return true;
    default:
        return false;
    }
}

static inline bool bbIsLastStmtTerminator(const AstNode *node)
{
    return nodeIs(node, BasicBlock) &&
           nodeIsTerminator(node->basicBlock.stmts.last);
}

static inline bool nodeIsBasicBlockEmpty(const AstNode *node)
{
    return nodeIs(node, BasicBlock) && node->basicBlock.stmts.last == NULL;
}

static inline void bbInsertAstNode(AstNode *bb, AstNode *node)
{
    node->parentScope = bb;
    insertAstNode(&bb->basicBlock.stmts, node);
}

static inline AstNode *createBasicBlock(LoweringContext *ctx,
                                        const AstNode *node)
{
    return makeBasicBlockAstNode(ctx->pool,
                                 &node->loc,
                                 flgNone,
                                 ctx->bbIndex++,
                                 ctx->currentFunction,
                                 NULL);
}

static inline AstNode *createJumpToBlock(LoweringContext *ctx, AstNode *bb)
{
    return makeBranchAstNode(ctx->pool, &bb->loc, flgNone, bb, NULL, NULL);
}

static void visitFuncDecl(AstVisitor *visitor, AstNode *node)
{
    LoweringContext *ctx = getAstVisitorContext(visitor);
    AstNode *body = node->funcDecl.body;
    if (body == NULL)
        return;
    const Type *type = node->type;

    ctx->bbIndex = 0;
    ctx->currentFunction = node;
    ctx->currentBB = createBasicBlock(ctx, body);
    ctx->funcEndBB = createBasicBlock(ctx, body);

    insertAstNode(&ctx->bbs, ctx->currentBB);
    if (!typeIs(type->func.retType, Void)) {
        AstNode *ret = makeVarDecl(
            ctx->pool,
            &node->loc,
            flgReturned,
            makeAnonymousVariable(ctx->strings, "ret"),
            makeTypeReferenceNode(ctx->pool, type->func.retType, &node->loc),
            NULL,
            NULL,
            type->func.retType);
        bbInsertAstNode(ctx->currentBB, ret);
        ctx->returnVar = ret;
    }

    if (nodeIs(body, BlockStmt)) {
        astVisit(visitor, body);
        body->blockStmt.stmts = ctx->bbs.first;
    }
    else {
        bbInsertAstNode(ctx->currentBB, body);
    }

    AstNode *lastBB = ctx->bbs.last;
    if (nodeIsBasicBlockEmpty(lastBB) || !bbIsLastStmtTerminator(lastBB))
        bbInsertAstNode(ctx->currentBB, createJumpToBlock(ctx, ctx->funcEndBB));

    insertAstNode(&ctx->bbs, ctx->funcEndBB);
    bbInsertAstNode(ctx->funcEndBB,
                    makeReturnAstNode(
                        ctx->pool,
                        &node->loc,
                        flgNone,
                        ctx->returnVar
                            ? makeUnaryExpr(ctx->pool,
                                            &node->loc,
                                            flgNone,
                                            true,
                                            opMove,
                                            makeResolvedIdentifier(
                                                ctx->pool,
                                                &node->loc,
                                                ctx->returnVar->_namedNode.name,
                                                0,
                                                ctx->returnVar,
                                                NULL,
                                                type->func.retType),
                                            NULL,
                                            type->func.retType)
                            : NULL,
                        NULL,
                        type->func.retType));

    ctx->bbs = (AstNodeList){};
    ctx->bbIndex = 0;
    ctx->currentBB = NULL;
    ctx->currentFunction = NULL;
    ctx->returnVar = NULL;
}

static void visitVarDecl(AstVisitor *visitor, AstNode *node)
{
    LoweringContext *ctx = getAstVisitorContext(visitor);
    if (hasFlag(node, TopLevelDecl))
        return;

    if (node->varDecl.init) {
        astVisit(visitor, node->varDecl.init);
    }
    bbInsertAstNode(ctx->currentBB, node);
}

static void visitBlockStmt(AstVisitor *visitor, AstNode *node)
{
    if (node->blockStmt.stmts == NULL)
        return;

    AstNode *stmt = node->blockStmt.stmts;
    for (; stmt;) {
        AstNode *curr = stmt;
        stmt = stmt->next;
        curr->next = NULL;
        astVisit(visitor, curr);
    }
}

static void visitExprStmt(AstVisitor *visitor, AstNode *node)
{
    LoweringContext *ctx = getAstVisitorContext(visitor);
    astVisit(visitor, node->exprStmt.expr);
    bbInsertAstNode(ctx->currentBB, node);
}

static void visitAsmStmt(AstVisitor *visitor, AstNode *node)
{
    LoweringContext *ctx = getAstVisitorContext(visitor);
    bbInsertAstNode(ctx->currentBB, node);
}

static void visitStmtExpr(AstVisitor *visitor, AstNode *node)
{
    LoweringContext *ctx = getAstVisitorContext(visitor);
    AstNode *body = node->stmtExpr.stmt;
    csAssert0(nodeIs(body, BlockStmt));

    AstNode *stmt = body->blockStmt.stmts;
    for (; stmt->next;) {
        AstNode *curr = stmt;
        stmt = stmt->next;
        curr->next = NULL;
        astVisit(visitor, curr);
    }
    csAssert0(nodeIs(stmt, ExprStmt));
    astVisit(visitor, stmt->exprStmt.expr);
    *node = *stmt->exprStmt.expr;
}

static void visitIfStmt(AstVisitor *visitor, AstNode *node)
{
    LoweringContext *ctx = getAstVisitorContext(visitor);
    AstNode *body = node->ifStmt.body, *otherwise = node->ifStmt.otherwise,
            *cond = node->ifStmt.cond;
    AstNode *currentBb = ctx->currentBB;

    AstNode *falseBB = NULL;
    AstNode *trueBB = createBasicBlock(ctx, body);
    AstNode *mergeBB = createBasicBlock(ctx, body);
    astVisit(visitor, cond);

    ctx->currentBB = trueBB;
    insertAstNode(&ctx->bbs, trueBB);
    astVisit(visitor, body);
    AstNode *lastBB = ctx->bbs.last;
    if (nodeIsBasicBlockEmpty(lastBB) || !bbIsLastStmtTerminator(lastBB))
        bbInsertAstNode(ctx->currentBB, createJumpToBlock(ctx, mergeBB));

    if (otherwise != NULL) {
        falseBB = createBasicBlock(ctx, otherwise);
        ctx->currentBB = falseBB;
        insertAstNode(&ctx->bbs, falseBB);
        astVisit(visitor, otherwise);
        lastBB = ctx->bbs.last;
        if (nodeIsBasicBlockEmpty(lastBB) || !bbIsLastStmtTerminator(lastBB))
            bbInsertAstNode(ctx->currentBB, createJumpToBlock(ctx, mergeBB));
    }

    bbInsertAstNode(currentBb,
                    makeBranchIfAstNode(ctx->pool,
                                        &cond->loc,
                                        flgNone,
                                        cond,
                                        trueBB,
                                        falseBB ?: mergeBB,
                                        NULL));

    insertAstNode(&ctx->bbs, mergeBB);
    ctx->currentBB = mergeBB;
    if (node->ifStmt.isTernary) {
        bbInsertAstNode(mergeBB,
                        makePhiAstNode(ctx->pool,
                                       &node->loc,
                                       flgNone,
                                       (AstNode *[]){trueBB, falseBB},
                                       2,
                                       NULL));
    }
}

static void visitWhileStmt(AstVisitor *visitor, AstNode *node)
{
    LoweringContext *ctx = getAstVisitorContext(visitor);
    AstNode *body = node->whileStmt.body, *cond = node->whileStmt.cond,
            *update = node->whileStmt.update;

    AstNode *condBB = createBasicBlock(ctx, cond);
    AstNode *bodyBB = createBasicBlock(ctx, body);
    AstNode *endBB = createBasicBlock(ctx, node);
    AstNode *updateBB = NULL;
    if (update != NULL)
        updateBB = createBasicBlock(ctx, node);

    ctx->updateBB = updateBB;
    ctx->condBB = condBB;
    ctx->endBB = endBB;

    bbInsertAstNode(ctx->currentBB, createJumpToBlock(ctx, condBB));

    insertAstNode(&ctx->bbs, condBB);
    astVisit(visitor, cond);
    bbInsertAstNode(
        condBB,
        makeBranchIfAstNode(
            ctx->pool, &cond->loc, flgNone, cond, bodyBB, endBB, NULL));

    insertAstNode(&ctx->bbs, bodyBB);
    ctx->currentBB = bodyBB;
    astVisit(visitor, body);
    if (updateBB) {
        bbInsertAstNode(ctx->currentBB, createJumpToBlock(ctx, updateBB));
        insertAstNode(&ctx->bbs, updateBB);
        ctx->currentBB = updateBB;
    }

    bbInsertAstNode(ctx->currentBB, createJumpToBlock(ctx, condBB));
    insertAstNode(&ctx->bbs, endBB);
    ctx->currentBB = endBB;
}

static void visitBreakStmt(AstVisitor *visitor, AstNode *node)
{
    LoweringContext *ctx = getAstVisitorContext(visitor);
    csAssert0(ctx->endBB);
    bbInsertAstNode(ctx->currentBB, createJumpToBlock(ctx, ctx->endBB));
    ctx->unreachable = true;
}

static void visitContinueStmt(AstVisitor *visitor, AstNode *node)
{
    LoweringContext *ctx = getAstVisitorContext(visitor);
    csAssert0(ctx->condBB);
    if (ctx->updateBB)
        bbInsertAstNode(ctx->currentBB, createJumpToBlock(ctx, ctx->currentBB));
    else
        bbInsertAstNode(ctx->currentBB, createJumpToBlock(ctx, ctx->condBB));
    ctx->unreachable = true;
}

static void visitReturnStmt(AstVisitor *visitor, AstNode *node)
{
    LoweringContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->returnStmt.expr;
    if (expr) {
        AstNode *var = ctx->returnVar;
        csAssert0(var);
        astVisit(visitor, expr);
        bbInsertAstNode(
            ctx->currentBB,
            makeAssignExpr(ctx->pool,
                           &expr->loc,
                           flgNone,
                           makeResolvedIdentifier(ctx->pool,
                                                  &expr->loc,
                                                  var->_namedNode.name,
                                                  0,
                                                  var,
                                                  NULL,
                                                  var->type),
                           opAssign,
                           expr,
                           NULL,
                           var->type));
    }
    bbInsertAstNode(ctx->currentBB, createJumpToBlock(ctx, ctx->funcEndBB));
}

static void visitSwitchStmt(AstVisitor *visitor, AstNode *node)
{
    LoweringContext *ctx = getAstVisitorContext(visitor);

    AstNode *currentBb = ctx->currentBB;
    AstNode *defaultBB = NULL;
    AstNode *endBB = createBasicBlock(ctx, node);

    DynArray cases = newDynArray(sizeof(SwitchIrCase));
    AstNode *case_ = node->switchStmt.cases;
    for (; case_; case_ = case_->next) {
        AstNode *bb = NULL;
        bb = createBasicBlock(ctx, case_);
        if (case_->caseStmt.match) {
            bb = createBasicBlock(ctx, case_);
            pushOnDynArray(
                &cases,
                &(SwitchIrCase){.bb = bb, .match = case_->caseStmt.match});
        }
        else {
            csAssert0(defaultBB == NULL);
            defaultBB = bb;
        }

        ctx->currentBB = bb;
        insertAstNode(&ctx->bbs, bb);
        astVisit(visitor, case_->caseStmt.body);
        // branch to the end
        AstNode *lastBB = ctx->bbs.last;
        if (nodeIsBasicBlockEmpty(lastBB) || !bbIsLastStmtTerminator(lastBB))
            bbInsertAstNode(ctx->currentBB, createJumpToBlock(ctx, endBB));
    }

    node->switchStmt.cond->parentScope = currentBb;
    bbInsertAstNode(currentBb,
                    makeSwitchIrAstNode(ctx->pool,
                                        &node->loc,
                                        flgNone,
                                        node->switchStmt.cond,
                                        defaultBB ?: endBB,
                                        cases.elems,
                                        cases.size,
                                        NULL));
    ctx->currentBB = endBB;
    insertAstNode(&ctx->bbs, endBB);

    freeDynArray(&cases);
}

static void visitMatchStmt(AstVisitor *visitor, AstNode *node)
{
    LoweringContext *ctx = getAstVisitorContext(visitor);

    AstNode *condition = makeGepAstNode(ctx->pool,
                                        &node->matchStmt.expr->loc,
                                        flgNone,
                                        node->matchStmt.expr,
                                        0,
                                        NULL);

    AstNode *currentBb = ctx->currentBB;
    AstNode *defaultBB = NULL;
    AstNode *endBB = createBasicBlock(ctx, node);

    DynArray cases = newDynArray(sizeof(SwitchIrCase));
    AstNode *case_ = node->matchStmt.cases;
    for (; case_; case_ = case_->next) {
        AstNode *bb = NULL;
        bb = createBasicBlock(ctx, case_);
        if (case_->caseStmt.match) {
            bb = createBasicBlock(ctx, case_);
            pushOnDynArray(
                &cases,
                &(SwitchIrCase){.bb = bb,
                                .match = makeIntegerLiteral(
                                    ctx->pool,
                                    &case_->caseStmt.match->loc,
                                    case_->caseStmt.idx,
                                    NULL,
                                    getPrimitiveType(ctx->types, prtU8))});
        }
        else {
            csAssert0(defaultBB == NULL);
            defaultBB = bb;
        }

        ctx->currentBB = bb;
        insertAstNode(&ctx->bbs, bb);
        if (case_->caseStmt.variable) {
            // visit the variable before generate code
            astVisit(visitor, case_->caseStmt.variable);
        }
        astVisit(visitor, case_->caseStmt.body);
        // branch to the end
        AstNode *lastBB = ctx->bbs.last;
        if (nodeIsBasicBlockEmpty(lastBB) || !bbIsLastStmtTerminator(lastBB))
            bbInsertAstNode(ctx->currentBB, createJumpToBlock(ctx, endBB));
    }

    condition->parentScope = currentBb;
    bbInsertAstNode(currentBb,
                    makeSwitchIrAstNode(ctx->pool,
                                        &node->loc,
                                        flgNone,
                                        condition,
                                        defaultBB ?: endBB,
                                        cases.elems,
                                        cases.size,
                                        NULL));
    ctx->currentBB = endBB;
    insertAstNode(&ctx->bbs, endBB);

    freeDynArray(&cases);
}

static void withSavedStack(Visitor func, AstVisitor *visitor, AstNode *node)
{
    LoweringContext *ctx = getAstVisitorContext(visitor);
    __typeof(ctx->stack) stack = ctx->stack;

    func(visitor, node);

    ctx->stack = stack;
}

AstNode *lowerAstNode(CompilerDriver *driver, AstNode *node)
{
    LoweringContext context = {.L = driver->L,
                               .types = driver->types,
                               .strings = driver->strings,
                               .pool = driver->pool};
    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context, {
        [astStmtExpr] = visitStmtExpr,
        [astExprStmt] = visitExprStmt,
        [astBlockStmt] = visitBlockStmt,
        [astWhileStmt] = visitWhileStmt,
        [astIfStmt] = visitIfStmt,
        [astContinueStmt] = visitContinueStmt,
        [astBreakStmt] = visitBreakStmt,
        [astReturnStmt] = visitReturnStmt,
        [astSwitchStmt] = visitSwitchStmt,
        [astMatchStmt] = visitMatchStmt,
        [astAsm] = visitAsmStmt,
        [astFuncDecl] = visitFuncDecl,
        [astVarDecl] = visitVarDecl,
        [astFieldDecl] = astVisitSkip,
        [astGenericDecl] = astVisitSkip,
        [astMacroDecl] = astVisitSkip
    }, .fallback = astVisitFallbackVisitAll, .dispatch = withSavedStack);
    // clang-format on

    astVisit(&visitor, node);
    return node;
}
