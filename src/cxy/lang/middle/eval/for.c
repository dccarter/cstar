//
// Created by Carter Mbotho on 2024-01-09.
//

#include "eval.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"

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

        const Type *type = evalType(ctx, body);
        if (type == NULL || typeIs(type, Error)) {
            node->tag = astError;
            return false;
        }

        if (nodeIs(body, BlockStmt) &&
            findAttribute(node, S_consistent) == NULL) {
            insertAstNode(nodes, body->blockStmt.stmts);
        }
        else {
            insertAstNode(nodes, body);
        }

        it = it->next;
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
        variable->varDecl.init = elem;

        const Type *type = evalType(ctx, body);
        if (type == NULL || typeIs(type, Error)) {
            node->tag = astError;
            return false;
        }

        if (nodeIs(body, BlockStmt) &&
            findAttribute(node, S_consistent) == NULL) {
            insertAstNode(nodes, body->blockStmt.stmts);
        }
        else {
            insertAstNode(nodes, body);
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

    const Type *tuple = unwrapType(range->type, NULL);
    if (tuple) {
        csAssert0(nodeIs(range, Identifier));
        u64 count = tuple->tuple.count;
        for (u64 i = 0; i < count; i++) {
            AstNode *body = deepCloneAstNode(ctx->pool, node->forStmt.body);
            variable->varDecl.init =
                makeMemberExpr(ctx->pool,
                               &range->loc,
                               range->flags,
                               makeResolvedIdentifier(ctx->pool,
                                                      &range->loc,
                                                      range->ident.value,
                                                      0,
                                                      range->ident.resolvesTo,
                                                      NULL,
                                                      range->type),
                               makeUnsignedIntegerLiteral(
                                   ctx->pool,
                                   &range->loc,
                                   i,
                                   NULL,
                                   getPrimitiveType(ctx->types, prtU64)),
                               NULL,
                               tuple->tuple.members[i]);

            const Type *type = evalType(ctx, body);
            if (type == NULL || typeIs(type, Error)) {
                node->tag = astError;
                return false;
            }

            if (nodeIs(body, BlockStmt) &&
                findAttribute(node, S_consistent) == NULL) {
                insertAstNode(nodes, body->blockStmt.stmts);
            }
            else {
                insertAstNode(nodes, body);
            }
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

        if (!nodeIs(body, Noop)) {
            if (nodeIs(body, BlockStmt) &&
                findAttribute(node, S_consistent) == NULL) {
                insertAstNode(nodes, body->blockStmt.stmts);
            }
            else {
                insertAstNode(nodes, body);
            }
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
    bool preserve = findAttribute(node, S_consistent) != NULL;
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
        if (nodeIs(body, BlockStmt) && !preserve && body->blockStmt.stmts) {
            body->blockStmt.stmts->parentScope = node->parentScope;
        }
        else
            body->parentScope = node->parentScope;

        const Type *type = evalType(ctx, body);
        if (type == NULL || typeIs(type, Error)) {
            node->tag = astError;
            return false;
        }

        if (nodeIs(body, BlockStmt) && !preserve) {
            insertAstNode(nodes, body->blockStmt.stmts);
        }
        else {
            insertAstNode(nodes, body);
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
    case astFuncParamDecl:
        if (!hasFlag(node->forStmt.range, Variadic)) {
            logError(ctx->L,
                     &rangeLoc,
                     "`#for` loop range expression is not comptime iterable, "
                     "parameter '{s}' is not variadic",
                     NULL);
            node->tag = astError;
            return;
        }
        node->forStmt.range->tag = astIdentifier;
        node->forStmt.range->ident.value =
            range.path.elements->pathElement.name;
        node->forStmt.range->ident.resolvesTo =
            range.path.elements->pathElement.resolvesTo;
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

    if (nodes.first == NULL) {
        node->tag = astNoop;
    }
    else {
        nodes.last->next = node->next;
        *node = *nodes.first;
    }
}
