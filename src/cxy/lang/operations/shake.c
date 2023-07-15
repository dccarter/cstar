//
// Created by Carter Mbotho on 2023-07-06.
//

#include "lang/ast.h"
#include "lang/flag.h"
#include "lang/operations.h"
#include "lang/visitor.h"

typedef struct {
    Log *L;
    MemPool *pool;
    StrPool *strPool;
} ShakeAstContext;

static AstNode *makeTupleMemberExpr(ShakeAstContext *ctx, AstNode *tuple, u64 i)
{
    if (tuple == NULL)
        return NULL;

    if (nodeIs(tuple, TupleExpr)) {
        AstNode *arg = tuple->tupleExpr.args;
        tuple->tupleExpr.args = arg->next;
        arg->next = NULL;
        return arg;
    }

    AstNode *target = NULL;
    if (nodeIs(tuple, VarDecl)) {
        target = makePathFromIdent(ctx->pool, tuple->varDecl.names);
    }
    else {
        csAssert0(nodeIs(tuple, Path) || nodeIs(tuple, Identifier));
        target = copyAstNode(ctx->pool, tuple);
    }

    return makeAstNode(
        ctx->pool,
        &tuple->loc,
        &(AstNode){
            .tag = astMemberExpr,
            .flags = tuple->flags,
            .memberExpr = {.target = target,
                           .member = makeAstNode(
                               ctx->pool,
                               &tuple->loc,
                               &(AstNode){.tag = astIntegerLit,
                                          .intLiteral.value = (i64)i})}});
}

static AstNode *shakeVariableInitializer(ShakeAstContext *ctx, AstNode *init)
{
    if (init == NULL || nodeIs(init, TupleExpr) || nodeIs(init, Identifier) ||
        (nodeIs(init, Path) && init->path.elements->next == NULL))
        return init;

    // Create variable for this
    return makeAstNode(
        ctx->pool,
        &init->loc,
        &(AstNode){.tag = astVarDecl,
                   .varDecl = {.names = makeGenIdent(
                                   ctx->pool, ctx->strPool, &init->loc),
                               .init = init}});
}

void visitVariableDecl(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    AstNode *names = node->varDecl.names, *init = node->varDecl.init,
            *type = node->varDecl.type, *name = names;

    if (names->next == NULL)
        return;

    AstNode *tuple = shakeVariableInitializer(ctx, init);

    AstNode *vars = NULL, *it = NULL;
    u64 i = 0;
    for (; name; i++) {
        AstNode *name_ = name;
        name = name->next;
        name_->next = NULL;

        if (isIgnoreVar(name_->ident.value)) {
            if (tuple == NULL) {
                logError(ctx->L,
                         &name_->loc,
                         "cannot use the builtin ignore `_` variable when "
                         "multi-variable declaration has no expression",
                         NULL);
                return;
            }
            continue;
        }

        AstNode *var = makeAstNode(
            ctx->pool,
            &name_->loc,
            &(AstNode){
                .tag = astVarDecl,
                .flags = node->flags | flgVisited,
                .varDecl = {.names = name_,
                            .type = copyAstNode(ctx->pool, type),
                            .init = makeTupleMemberExpr(ctx, tuple, i)}});
        if (vars == NULL) {
            vars = var;
            it = var;
        }
        else {
            it->next = var;
            it = var;
        }
    }

    it->next = node->next;
    if (tuple == init) {
        *node = *vars;
    }
    else {
        tuple->next = vars;
        *node = *tuple;
    }
}

void visitIfStmt(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    AstNode *cond = node->ifStmt.cond, *ifNode = node;
    if (nodeIs(cond, VarDecl)) {
        AstNode *var = makeAstNode(
            ctx->pool,
            &cond->loc,
            &(AstNode){
                .tag = astVarDecl,
                .flags = cond->flags,
                .next = duplicateAstNode(ctx->pool, node),
                .varDecl = {
                    .names = makeGenIdent(
                        ctx->pool, ctx->strPool, &cond->varDecl.names->loc),
                    .init = cond->varDecl.init,
                    .type = cloneAstNode(ctx->pool, cond->varDecl.type)}});

        ifNode = var->next;
        ifNode->ifStmt.cond = makePathFromIdent(ctx->pool, var->varDecl.names);
        if (!nodeIs(ifNode->ifStmt.body, BlockStmt)) {
            ifNode->ifStmt.body = makeAstNode(
                ctx->pool,
                &ifNode->ifStmt.body->loc,
                &(AstNode){.tag = astBlockStmt,
                           .blockStmt = {.stmts = ifNode->ifStmt.body}});
        }

        cond->next = ifNode->ifStmt.body->blockStmt.stmts;
        cond->varDecl.init = makeAstNode(
            ctx->pool,
            &cond->loc,
            &(AstNode){.tag = astUnaryExpr,
                       .unaryExpr = {.op = opAddrOf,
                                     .isPrefix = true,
                                     .operand = makePathFromIdent(
                                         ctx->pool, var->varDecl.names)}});
        ifNode->ifStmt.body->blockStmt.stmts = cond;
        *node = *var;
        ifNode = node->next;
    }

    astVisit(visitor, ifNode->ifStmt.body);
    if (ifNode->ifStmt.otherwise)
        astVisit(visitor, ifNode->ifStmt.otherwise);
}

void visitWhileStmt(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    AstNode *cond = node->whileStmt.cond, *whileNode = node;
    if (nodeIs(cond, VarDecl)) {
        AstNode *var = makeAstNode(
            ctx->pool,
            &cond->loc,
            &(AstNode){
                .tag = astVarDecl,
                .flags = cond->flags,
                .next = duplicateAstNode(ctx->pool, node),
                .varDecl = {
                    .names = makeGenIdent(
                        ctx->pool, ctx->strPool, &cond->varDecl.names->loc),
                    .init = NULL,
                    .type = cloneAstNode(ctx->pool, cond->varDecl.type)}});

        whileNode = var->next;
        whileNode->whileStmt.cond = makeAstNode(
            ctx->pool,
            &cond->loc,
            &(AstNode){.tag = astGroupExpr,
                       .groupExpr.expr = makeAstNode(
                           ctx->pool,
                           &cond->loc,
                           &(AstNode){.tag = astAssignExpr,
                                      .assignExpr = {
                                          .op = opAssign,
                                          .lhs = makePathFromIdent(
                                              ctx->pool, var->varDecl.names),
                                          .rhs = cond->varDecl.init}})});

        if (!nodeIs(whileNode->whileStmt.body, BlockStmt)) {
            whileNode->whileStmt.body = makeAstNode(
                ctx->pool,
                &whileNode->whileStmt.body->loc,
                &(AstNode){.tag = astBlockStmt,
                           .blockStmt = {.stmts = whileNode->whileStmt.body}});
        }

        cond->next = whileNode->whileStmt.body->blockStmt.stmts;
        cond->varDecl.init = makeAstNode(
            ctx->pool,
            &cond->loc,
            &(AstNode){.tag = astUnaryExpr,
                       .unaryExpr = {.op = opAddrOf,
                                     .isPrefix = true,
                                     .operand = makePathFromIdent(
                                         ctx->pool, var->varDecl.names)}});
        whileNode->whileStmt.body->blockStmt.stmts = cond;
        *node = *var;
        whileNode = node->next;
    }

    astVisit(visitor, whileNode->whileStmt.body);
}

AstNode *shakeAstNode(CompilerDriver *driver, AstNode *node)
{
    ShakeAstContext context = {
        .L = driver->L, .pool = &driver->pool, .strPool = &driver->strPool};

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context, {
        [astVarDecl] = visitVariableDecl,
        [astIfStmt] = visitIfStmt,
        [astWhileStmt] = visitWhileStmt,

    }, .fallback = astVisitFallbackVisitAll);
    // clang-format on

    astVisit(&visitor, node);

    return node;
}
