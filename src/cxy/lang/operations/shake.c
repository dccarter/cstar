//
// Created by Carter Mbotho on 2023-07-06.
//

#include "lang/operations.h"
#include "lang/visitor.h"

typedef struct {
    Log *L;
    MemPool *pool;
    StrPool *strPool;
} ShakeAstContext;

AstNode *visitVariableDecl(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    bool isMultipleVariable = node->varDecl.names->next != NULL;
    if (!isMultipleVariable)
        return node;

    AstNode *names = node->varDecl.names, *init = node->varDecl.init,
            *type = node->varDecl.type;
    if (init != NULL) {
        if (!nodeIs(init, TupleExpr)) {
            logError(ctx->L,
                     &init->loc,
                     "unexpected multiple variable initializer expression, "
                     "expecting a tuple expression",
                     NULL);
            return node;
        }

        u64 namesCount = countAstNodes(names),
            initCount = countAstNodes(init->tupleExpr.args);
        if (namesCount > initCount) {
            logError(ctx->L,
                     locExtend(&(FileLoc){},
                               &names->loc,
                               &getLastAstNode(names)->loc),
                     "number of variables exceed number of initializer "
                     "elements in tuple, expecting {u64} or less, got {u64}",
                     (FormatArg[]){{.u64 = initCount}, {.u64 = namesCount}});
            return node;
        }
    }
    else if (node->varDecl.type == NULL) {
        logError(ctx->L,
                 locAfter(&(FileLoc){}, &getLastAstNode(names)->loc),
                 "multiple variables types required when initializer is not "
                 "provided",
                 NULL);
        return node;
    }

    AstNode *name = names, *value = init ? init->tupleExpr.args : NULL;
    AstNode *vars = NULL, *next = NULL;
    for (; name; name = name->next, value = value ? value->next : NULL) {
        if (isIgnoreVar(name->ident.value)) {
            if (init == NULL) {
                logError(
                    ctx->L,
                    &name->loc,
                    "ignore variable '_' usage in multi-variable declaration "
                    "allowed only when an initializer is specified",
                    NULL);
                return node;
            }
            continue;
        }

        AstNode *var = makeAstNode(
            ctx->pool,
            &node->loc,
            &(AstNode){.tag = astVarDecl,
                       .flags = node->flags,
                       .varDecl = {.names = copyAstNode(ctx->pool, name),
                                   .init = copyAstNode(ctx->pool, value),
                                   .type = type ? cloneAstNode(ctx->pool, type)
                                                : NULL}});
        if (vars == NULL) {
            vars = next = var;
        }
        else {
            next = next->next = var;
        }
    }

    if (vars == NULL) {
        logWarning(ctx->L,
                   &node->loc,
                   "pointless multi-variable declaration can be replaced with "
                   "expression",
                   NULL);

        init->next = node->next;
        *node = *init;
    }
    else {
        next->next = node->next;
        *node = *vars;
    }

    return node;
}

AstNode *visitIfStmt(AstVisitor *visitor, AstNode *node)
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

    return node;
}

AstNode *visitWhileStmt(AstVisitor *visitor, AstNode *node)
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
    return node;
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
