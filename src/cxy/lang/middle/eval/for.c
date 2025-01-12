//
// Created by Carter Mbotho on 2024-01-09.
//

#include "eval.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"

static AstNode *getEvaluatedBody(const AstNode *loop, AstNode *body)
{
    if (!nodeIs(body, BlockStmt) || findAttribute(loop, S_consistent))
        return body;
    return body->blockStmt.stmts;
}

static bool evalExprForStmtIterable(AstVisitor *visitor,
                                    AstNode *node,
                                    AstNodeList *nodes)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    AstNode *range = node->forStmt.range, *variable = node->forStmt.var;

    AstNode *it = nodeIs(range, ComptimeOnly) ? range->next : range;
    while (it) {
        AstNode *body = deepCloneAstNode(ctx->pool, node->forStmt.body);
        variable->varDecl.init = it;
        it = it->next;
        body->parentScope = node->parentScope;

        const Type *type = evalType(ctx, body);
        if (type == NULL || typeIs(type, Error)) {
            node->tag = astError;
            return false;
        }

        body = getEvaluatedBody(node, body);
        while (body) {
            AstNode *tmp = body;
            body = body->next;
            if (isNoopNodeAfterEval(tmp))
                continue;
            tmp->parentScope = node->parentScope;
            tmp->next = NULL;
            insertAstNode(nodes, tmp);
        }
    }

    return true;
}

static bool evalExprForStmtArray(AstVisitor *visitor,
                                 AstNode *node,
                                 AstNodeList *nodes)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    AstNode *range = node->forStmt.range, *elem = range->arrayExpr.elements,
            *variable = node->forStmt.var;

    for (; elem; elem = elem->next) {
        AstNode *body = deepCloneAstNode(ctx->pool, node->forStmt.body);
        body->parentScope = node->parentScope;
        variable->varDecl.init = elem;

        const Type *type = evalType(ctx, body);

        if (type == NULL || typeIs(type, Error)) {
            node->tag = astError;
            return false;
        }

        body = getEvaluatedBody(node, body);
        while (body) {
            AstNode *tmp = body;
            body = body->next;
            if (isNoopNodeAfterEval(tmp))
                continue;
            tmp->parentScope = node->parentScope;
            tmp->next = NULL;
            insertAstNode(nodes, tmp);
        }
    }

    return true;
}

static bool evalExprForStmtVariadic(AstVisitor *visitor,
                                    AstNode *node,
                                    AstNodeList *nodes)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    AstNode *range = node->forStmt.range, *variable = node->forStmt.var;

    csAssert0(nodeIs(range, Identifier));
    range = range->ident.resolvesTo;
    if (typeIs(range->type, Void))
        return true;

    for (; range; range = range->next) {
        AstNode *body = deepCloneAstNode(ctx->pool, node->forStmt.body);
        body->parentScope = node->parentScope;
        variable->varDecl.init = makeResolvedIdentifier(ctx->pool,
                                                        &range->loc,
                                                        range->funcDecl.name,
                                                        0,
                                                        range,
                                                        NULL,
                                                        range->type);
        const Type *type = evalType(ctx, body);
        if (type == NULL || typeIs(type, Error)) {
            node->tag = astError;
            return false;
        }

        body = getEvaluatedBody(node, body);
        while (body) {
            AstNode *tmp = body;
            body = body->next;
            if (isNoopNodeAfterEval(tmp))
                continue;
            tmp->parentScope = node->parentScope;
            tmp->next = NULL;
            insertAstNode(nodes, tmp);
        }
    }

    return true;
}

static bool evalForStmtWithString(AstVisitor *visitor,
                                  AstNode *node,
                                  AstNodeList *nodes)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    AstNode *range = node->forStmt.range, *variable = node->forStmt.var;

    u64 count = strlen(range->stringLiteral.value);
    for (u64 i = 0; i < count; i++) {
        AstNode *body = deepCloneAstNode(ctx->pool, node->forStmt.body);
        body->parentScope = node->parentScope;
        variable->varDecl.init = makeAstNode(
            ctx->pool,
            &range->loc,
            &(AstNode){.tag = astCharLit,
                       .charLiteral.value = range->stringLiteral.value[i]});

        const Type *type = evalType(ctx, body);
        if (type == NULL || typeIs(type, Error)) {
            node->tag = astError;
            return false;
        }

        body = getEvaluatedBody(node, body);
        while (body) {
            AstNode *tmp = body;
            body = body->next;
            if (isNoopNodeAfterEval(tmp))
                continue;
            tmp->parentScope = node->parentScope;
            tmp->next = NULL;
            insertAstNode(nodes, tmp);
        }
    }

    return true;
}

static bool evalForStmtWithRange(AstVisitor *visitor,
                                 AstNode *node,
                                 AstNodeList *nodes)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    AstNode *range = node->forStmt.range, *variable = node->forStmt.var;
    i64 i = integerLiteralValue(range->rangeExpr.start),
        count = integerLiteralValue(range->rangeExpr.end),
        step = range->rangeExpr.step
                   ? integerLiteralValue(range->rangeExpr.step)
                   : 1;

    for (; i < count; i += step) {
        AstNode *body = deepCloneAstNode(ctx->pool, node->forStmt.body);
        variable->varDecl.init = makeAstNode(
            ctx->pool,
            &range->loc,
            &(AstNode){.tag = astIntegerLit, .intLiteral.value = i});

        if (body) {
            body->parentScope = node->parentScope;

            const Type *type = evalType(ctx, body);
            if (type == NULL || typeIs(type, Error)) {
                node->tag = astError;
                return false;
            }

            body = getEvaluatedBody(node, body);
            while (body) {
                AstNode *tmp = body;
                body = body->next;
                if (isNoopNodeAfterEval(tmp))
                    continue;
                tmp->parentScope = node->parentScope;
                tmp->next = NULL;
                insertAstNode(nodes, tmp);
            }
        }
    }

    return true;
}

void evalForStmt(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    FileLoc rangeLoc = node->forStmt.range->loc;
    AstNode range = *node->forStmt.range;
    if (!evaluate(visitor, node->forStmt.range)) {
        node->tag = astError;
        return;
    }

    AstNodeList nodes = {NULL};

    switch (node->forStmt.range->tag) {
    case astRangeExpr:
        if (!evalForStmtWithRange(visitor, node, &nodes))
            return;
        break;
    case astStringLit:
        if (!evalForStmtWithString(visitor, node, &nodes))
            return;
        break;
    case astArrayExpr:
        if (!evalExprForStmtArray(visitor, node, &nodes))
            return;
        break;
    case astIdentifier:
        if (!hasFlag(node->forStmt.range, Variadic)) {
            logError(ctx->L,
                     &rangeLoc,
                     "`#for` loop range expression is not comptime iterable, "
                     "parameter '{s}' is not variadic",
                     NULL);
            node->tag = astError;
            return;
        }
        if (!evalExprForStmtVariadic(visitor, node, &nodes))
            return;
        break;
    default:
        if (!hasFlag(node->forStmt.range, ComptimeIterable)) {
            logError(ctx->L,
                     &rangeLoc,
                     "`#for` loop range expression is not comptime iterable",
                     NULL);
            node->tag = astError;
            return;
        }
        if (!evalExprForStmtIterable(visitor, node, &nodes))
            return;
        break;
    }

    if (nodes.first != NULL) {
        nodes.last->next = node->next;
        node->next = nodes.first;
    }
    node->tag = astNoop;
}
