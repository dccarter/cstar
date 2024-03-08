//
// Created by Carter Mbotho on 2024-01-09.
//

#include "check.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"

#include <string.h>

static inline bool isSupportedOnAuto(AstNode *node)
{
    switch (node->tag) {
    case astIntegerLit:
    case astFloatLit:
    case astCharLit:
    case astBoolLit:
    case astStringLit:
    case astNullLit:
    case astTupleExpr:
    case astArrayExpr:
    case astStructExpr:
        return true;
    default:
        return false;
    }
}

static const Type *checkNewInitializerOverload(AstVisitor *visitor,
                                               AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *init = node->newExpr.init;
    AstNode *callee = init->callExpr.callee;

    // turn new S(...) => ({ var tmp = new S{}; tmp.init(...); tmp; })

    cstring name = makeAnonymousVariable(ctx->strings, "_new_tmp");
    // new S{}
    AstNode *newExpr = makeNewExpr(
        ctx->pool, &callee->loc, callee->flags, callee, NULL, NULL, NULL);
    // var name = new S{}
    AstNode *varDecl = makeVarDecl(ctx->pool,
                                   &callee->loc,
                                   callee->flags | flgImmediatelyReturned,
                                   name,
                                   NULL,
                                   newExpr,
                                   NULL,
                                   NULL);
    // tmp.init
    AstNode *newCallee = makePathWithElements(
        ctx->pool,
        &callee->loc,
        callee->flags,
        makeResolvedPathElement(ctx->pool,
                                &callee->loc,
                                name,
                                callee->flags,
                                varDecl,
                                makePathElement(ctx->pool,
                                                &callee->loc,
                                                S_InitOverload,
                                                callee->flags,
                                                NULL,
                                                NULL),
                                NULL),
        NULL);

    // tmp;
    AstNode *ret = makeExprStmt(
        ctx->pool,
        &init->loc,
        init->flags,
        makeResolvedPath(
            ctx->pool, &init->loc, name, flgNone, varDecl, NULL, NULL),
        NULL,
        NULL);

    //     name.init(...); tmp
    varDecl->next = makeCallExpr(ctx->pool,
                                 &init->loc,
                                 newCallee,
                                 init->callExpr.args,
                                 init->flags,
                                 ret,
                                 NULL);

    memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);
    clearAstBody(node);
    node->tag = astStmtExpr;
    node->stmtExpr.stmt =
        makeBlockStmt(ctx->pool, &init->loc, varDecl, NULL, NULL);
    node->stmtExpr.stmt->flags |= flgBlockReturns;

    return checkType(visitor, node);
}


static const Type *checkNewInitializerExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *init = node->newExpr.init;

    if (isSupportedOnAuto(init))
        return checkType(visitor, init);

    if (nodeIs(init, CallExpr)) {
        if (!hasFlag(init->callExpr.callee, TypeAst)) {
            logError(ctx->L,
                     &init->callExpr.callee->loc,
                     "only types can be created using the new expression",
                     NULL);

            return NULL;
        }

        const Type *callee = checkType(visitor, init->callExpr.callee);
        if (typeIs(callee, Interface)) {
            logError(ctx->L,
                     &init->callExpr.callee->loc,
                     "cannot use `new` operator on type `{t}`, "
                     "interface's cannot be allocated",
                     (FormatArg[]){{.t = callee}});
            return NULL;
        }
        else if (typeIs(callee, Struct)) {
            if (!findStructMemberType(callee, S_InitOverload)) {
                logError(ctx->L,
                         &init->callExpr.callee->loc,
                         "cannot use `new` constructor expression on type "
                         "'{t}', structure does not overload new operator",
                         (FormatArg[]){{.t = callee}});
                return NULL;
            }
            return checkNewInitializerOverload(visitor, node);
        }
        else {
            if (init->callExpr.args == NULL) {
                node->newExpr.init = NULL;
                return callee;
            }
            if (init->callExpr.args->next) {
                logError(ctx->L,
                         &init->callExpr.args->next->loc,
                         "`new` initializer expression for type '{t}' accepts "
                         "only 1 parameter",
                         (FormatArg[]){{.t = callee}});
                return NULL;
            }
            node->newExpr.init = init->callExpr.args;
            const Type *type = checkType(visitor, node->newExpr.init);
            if (typeIs(type, Error))
                return NULL;

            if (!isExplicitConstructableFrom(ctx, callee, type)) {
                logError(ctx->L,
                         &init->callExpr.args->loc,
                         "type '{t}' cannot be constructed with value of "
                         "type '{t}'",
                         (FormatArg[]){{.t = callee}, {.t = type}});
                return NULL;
            }
            return callee;
        }
    }

    return NULL;
}

void checkNewExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *type = NULL, *init = NULL;
    u64 flags = flgNone;

    if (node->newExpr.type) {
        flags = node->newExpr.type->flags;
        type = checkType(visitor, node->newExpr.type);
        if (typeIs(type, Error)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }
    }

    if (node->newExpr.init) {
        if (flags == flgNone)
            flags = node->newExpr.init->flags;

        init = checkNewInitializerExpr(visitor, node);
        if (init == NULL) {
            logError(ctx->L,
                     &node->loc,
                     "`new` operator syntax not supported",
                     NULL);
            node->type = ERROR_TYPE(ctx);
            return;
        }
        if (nodeIs(node, StmtExpr))
            return;
    }

    if (type == NULL) {
        type = init;
    }

    if (init && !isTypeAssignableFrom(type, init)) {
        logError(ctx->L,
                 &node->loc,
                 "new initializer value type '{t}' is not assignable to "
                 "type '{t}'",
                 (FormatArg[]){{.t = type}, {.t = init}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    node->flags = flags;
    node->type = makePointerType(ctx->types, type, type->flags);
}
