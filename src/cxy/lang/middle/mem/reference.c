//
// Created by Carter Mbotho on 2024-07-11.
//
#include "mem.h"

#include "core/alloc.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/visitor.h"

static bool isIdentifier(const AstNode *node)
{
    if (node == NULL)
        return false;

    switch (node->tag) {
    case astIdentifier:
        return true;
    case astCastExpr:
    case astTypedExpr:
        return isIdentifier(node->castExpr.expr);
    case astGroupExpr:
        return isIdentifier(node->groupExpr.expr);
    case astPointerOf:
    case astReferenceOf:
    case astUnaryExpr: {
        Operator op = node->unaryExpr.op;
        if (op == opMove || op == opPtrof || op == opRefof || op == opDeref)
            return isIdentifier(node->unaryExpr.operand);
    }
    default:
        return false;
    }
}

const AstNode *resolveCallExpr(const AstNode *node)
{
    if (node == NULL)
        return NULL;

    switch (node->tag) {
    case astCallExpr:
        return node;
    case astCastExpr:
    case astTypedExpr:
        return resolveCallExpr(node->castExpr.expr);
    case astGroupExpr:
        return resolveCallExpr(node->groupExpr.expr);
    case astPointerOf:
    case astReferenceOf:
    case astUnaryExpr: {
        Operator op = node->unaryExpr.op;
        if (op == opMove || op == opPtrof || opRefof || op == opDeref)
            return resolveCallExpr(node->unaryExpr.operand);
    }
    default:
        return NULL;
    }
}

static bool isReference(const AstNode *node)
{
    return (node != NULL) && hasFlag(node, Reference) &&
           (isClassType(node->type) || hasReferenceMembers(node->type));
}

static inline bool isReferenceExpr(AstNode *node)
{
    if (!node || !isClassType(node->type) && !hasReferenceMembers(node->type))
        return false;
    const AstNode *resolved = resolveIdentifier(node);
    if (resolved == NULL) {
        const AstNode *caller = resolveCallExpr(node);
        if (caller == NULL)
            return false;
        resolved = getTypeDecl(caller->callExpr.callee->type);
    }
    return hasFlag(resolved, Reference);
}

static void visitReturnStmt(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->returnStmt.expr;
    if (expr == NULL)
        return;

    astVisit(visitor, expr);
    if (isReferenceExpr(expr)) {
        logError(ctx->L,
                 &node->loc,
                 "returning a reference expression from a function is not "
                 "allowed",
                 NULL);
        return;
    }

    AstNode *ident = resolveIdentifier(expr);
    if (ident != NULL && !nodeIsThisParam(ident)) {
        ident->flags |= flgReturned;
    }
}

static void visitCallExpr(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    AstNode *callee = node->callExpr.callee, *args = node->callExpr.args;
    AstNode *func = getTypeDecl(callee->type),
            *params = nodeGetFuncParams(func);
    if (hasFlag(func, Extern))
        return;

    AstNode *arg = args, *param = params;
    if (nodeIsThisParam(param)) {
        param = param->next;
        arg = arg ? arg->next : arg;
    }
    for (; arg; arg = arg->next, param = param->next) {
        astVisit(visitor, arg);

        if (isReferenceExpr(arg)) {
            if (!hasFlag(param, Reference))
                logError(
                    ctx->L,
                    &arg->loc,
                    "passing a reference expression as an argument to a non "
                    "reference function parameter is not allowed",
                    NULL);
        }
        else if (isReference(param) && !nodeIsLeftValue(arg)) {
            logError(ctx->L,
                     &arg->loc,
                     "passing an r-value to a reference function parameter is "
                     "not supported",
                     NULL);
        }
    }
}

static void visitAssignmentExpr(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    AstNode *lhs = node->assignExpr.lhs, *rhs = node->assignExpr.rhs;

    if (!isReferenceExpr(rhs)) {
        astVisit(visitor, lhs);
        astVisit(visitor, rhs);
        return;
    }

    if (!isReferenceExpr(lhs)) {
        logError(ctx->L,
                 &rhs->loc,
                 "assigning a reference expression to a non reference variable "
                 "is not allowed",
                 NULL);
    }
}

static void visitTupleExpr(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    AstNode *elem = node->tupleExpr.elements;
    for (; elem; elem = elem->next) {
        if (isReferenceExpr(elem)) {
            logError(
                ctx->L,
                &elem->loc,
                "passing a reference expression to a tuple expression is not "
                "allowed",
                NULL);
        }
    }
}

static void visitStructExpr(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    AstNode *field = node->structExpr.fields;
    for (; field; field = field->next) {
        if (isReferenceExpr(field->fieldExpr.value)) {
            logError(ctx->L,
                     &field->loc,
                     "passing a reference expression to a field expression is "
                     "not allowed",
                     NULL);
        }
    }
}

static void visitVarDecl(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->varDecl.init;
    if (expr == NULL)
        return;

    astVisit(visitor, expr);
    if (isReferenceExpr(expr) && !hasFlag(node, Reference)) {
        logError(ctx->L,
                 &expr->loc,
                 "initializing a non-reference variable declaration with a "
                 "reference expression is not allowed",
                 NULL);
    }
    else if (hasFlag(node, Reference) &&
             !(nodeIsLeftValue(expr) || isLiteralExpr(expr))) {
        logError(ctx->L,
                 &expr->loc,
                 "initializing a reference variable declaration with a "
                 "non-reference expression is not allowed",
                 NULL);
    }
}

void checkReferenceVariables(MMContext *context, AstNode *node)
{

    // clang-format off
    AstVisitor visitor = makeAstVisitor(context, {
        [astCallExpr] = visitCallExpr,
        [astAssignExpr] = visitAssignmentExpr,
        [astTupleExpr] = visitTupleExpr,
        [astStructExpr] = visitStructExpr,
        [astReturnStmt] = visitReturnStmt,
        [astVarDecl] = visitVarDecl,
        [astGenericDecl] = astVisitSkip,
        [astMacroDecl] = astVisitSkip
    }, .fallback = astVisitFallbackVisitAll, .dispatch = withSavedStack);
    // clang-format on

    blockScopeContainerInit(&context->bsc, sizeof(VariableTrace));
    astVisit(&visitor, node);
    blockScopeContainerDeinit(&context->bsc);
}