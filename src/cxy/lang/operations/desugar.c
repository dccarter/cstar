//
// Created by Carter Mbotho on 2023-07-06.
//

#include <driver/driver.h>
#include <lang/ast.h>
#include <lang/visitor.h>

typedef struct {
    Env env;
    Log *L;
    MemPool *pool;
} DesugarContext;

AstNode *visitVariableDecl(AstVisitor *visitor, AstNode *node)
{
    DesugarContext *ctx = getAstVisitorContext(visitor);
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
        if (name->ident.value[0] == '_' && name->ident.value[1] == '\0') {
            if (init == NULL) {
                logError(
                    ctx->L,
                    &name->loc,
                    "ignore variable '_' usage in multi-variable declaration "
                    "allowed only when an initializer is specified",
                    NULL);
                return node;
            }
        }

        AstNode *var = makeAstNode(
            ctx->pool,
            &node->loc,
            &(AstNode){.tag = astVarDecl,
                       .flags = node->flags,
                       .varDecl = {.names = names,
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
    DesugarContext *ctx = getAstVisitorContext(visitor);
    AstNode *cond = node->ifStmt.cond;
    if (nodeIs(cond, VarDecl)) {
        AstNode *var = makeAstNode(
            ctx->pool,
            &cond->loc,
            &(AstNode){
                .tag = astVarDecl,
                .flags = cond->flags,
                .varDecl = {
                    .names = makeGenIdent(ctx->pool, &cond->varDecl.names->loc),
                    .init = cond->varDecl.init,
                    .type = cloneAstNode(ctx->pool, cond->varDecl.type)}});

        node->ifStmt.cond = makePathFromIdent(ctx->pool, var->varDecl.names);
        if (!nodeIs(node->ifStmt.body, BlockStmt)) {
            node->ifStmt.body = makeAstNode(
                ctx->pool,
                &node->ifStmt.body->loc,
                &(AstNode){.tag = astBlockStmt,
                           .blockStmt = {.stmts = node->ifStmt.body}});
        }

        cond->next = node->ifStmt.body->blockStmt.stmts;
        cond->varDecl.init = makePathFromIdent(ctx->pool, var->varDecl.names);
        node->ifStmt.body->blockStmt.stmts = cond;
    }

    astVisit(visitor, node->ifStmt.body);
    if (node->ifStmt.otherwise)
        astVisit(visitor, node->ifStmt.otherwise);

    return node;
}

AstNode *visitWhileStmt(AstVisitor *visitor, AstNode *node)
{
    DesugarContext *ctx = getAstVisitorContext(visitor);
    AstNode *cond = node->whileStmt.cond;
    if (nodeIs(cond, VarDecl)) {
        AstNode *var = makeAstNode(
            ctx->pool,
            &cond->loc,
            &(AstNode){
                .tag = astVarDecl,
                .flags = cond->flags,
                .varDecl = {
                    .names = makeGenIdent(ctx->pool, &cond->varDecl.names->loc),
                    .init = cond->varDecl.init,
                    .type = cloneAstNode(ctx->pool, cond->varDecl.type)}});

        node->whileStmt.cond = makePathFromIdent(ctx->pool, var->varDecl.names);
        if (!nodeIs(node->whileStmt.body, BlockStmt)) {
            node->whileStmt.body = makeAstNode(
                ctx->pool,
                &node->whileStmt.body->loc,
                &(AstNode){.tag = astBlockStmt,
                           .blockStmt = {.stmts = node->whileStmt.body}});
        }

        cond->next = node->whileStmt.body->blockStmt.stmts;
        cond->varDecl.init = makePathFromIdent(ctx->pool, var->varDecl.names);
        node->whileStmt.body->blockStmt.stmts = cond;
    }

    if (node->whileStmt.body)
        astVisit(visitor, node->whileStmt.body);

    return node;
}

AstNode *visitForStmt(AstVisitor *visitor, AstNode *node)
{
    DesugarContext *ctx = getAstVisitorContext(visitor);
    if (nodeIs(node->forStmt.range, RangeExpr))
        return node;
}

AstNode *desugarNodes(CompilerDriver *driver, AstNode *node)
{
    DesugarContext context = {};
    environmentInit(&context.env);

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context, {
        [astVarDecl] = visitVariableDecl,
        [astIfStmt] = visitIfStmt,
        [astWhileStmt] = visitWhileStmt,
    });

    // clang-format on

    astVisit(&visitor, node);

    return node;
}