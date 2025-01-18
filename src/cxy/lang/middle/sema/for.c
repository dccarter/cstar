//
// Created by Carter Mbotho on 2024-01-09.
//

#include "check.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"

static const Type *findCustomRange(TypingContext *ctx,
                                   const Type *type,
                                   const FileLoc *loc,
                                   u64 flags)
{
    type = stripReference(type);
    const Type *rangeOp = findMemberInType(type, S_Range);
    if (rangeOp) {
        rangeOp = matchOverloadedFunction(
            ctx->L, rangeOp, (const Type *[]){}, 0, loc, flags & flgConst);
    }
    type = getTypeBase(type);
    if (type) {
        return findCustomRange(ctx, type, loc, flags);
    }

    return rangeOp;
}

static const Type *findIteratorType(TypingContext *ctx, AstNode *range)
{
    const Type *rangeOp =
        findCustomRange(ctx, range->type, &range->loc, range->flags);
    if (rangeOp == NULL) {
        logError(ctx->L,
                 &range->loc,
                 "type `{t}` does not have a range overload operator",
                 (FormatArg[]){{.t = range->type}});
        return NULL;
    }

    csAssert0(typeIs(rangeOp, Func) && typeIs(rangeOp->func.retType, Class) &&
              hasFlag(rangeOp->func.retType, Closure));

    const Type *iterator =
        findStructMemberType(rangeOp->func.retType, S_CallOverload);
    csAssert0(typeIs(iterator, Func));

    return iterator;
}

static bool checkForStmtCustomRange(TypingContext *ctx,
                                    AstNode *node,
                                    u64 variablesCount,
                                    const Type *range)
{
    AstNode *variable = node->forStmt.var;

    const Type *iterator = findIteratorType(ctx, node->forStmt.range);
    if (iterator == NULL)
        return false;

    const Type *value = getOptionalTargetType(iterator->func.retType);
    if (variable->next == NULL) {
        variable->type = value;
        return true;
    }

    if (!typeIs(value, Tuple)) {
        logError(ctx->L,
                 &node->forStmt.range->loc,
                 "multi-variable `for` loop used with a custom iterator "
                 "requires an iterator that returns a tuple",
                 NULL);

        logNote(ctx->L,
                &iterator->func.decl->loc,
                "iterator defined here returns '{t}'",
                (FormatArg[]){{.t = value}});
        return false;
    }

    if (variablesCount > value->tuple.count) {
        logError(
            ctx->L,
            manyNodesLoc(variable),
            "unexpected number of variables got {u64}, expecting at most {u64}",
            (FormatArg[]){{.u64 = variablesCount},
                          {.u64 = value->tuple.count}});
        return false;
    }

    for (u64 i = 0; variable; variable = variable->next, i++) {
        if (isIgnoreVar(variable->varDecl.name))
            continue;

        const Type *member = value->tuple.members[i];
        if (hasFlag(member, Const) && !hasFlag(variable, Const)) {
            logError(ctx->L,
                     &variable->loc,
                     "cannot bind a const value to a non constant variable",
                     NULL);
            return false;
        }
        variable->type = member;
    }

    return true;
}

static void transformForCustomRange(AstVisitor *visitor,
                                    AstNode *node,
                                    u64 variablesCount)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *type = NULL;
    AstNode *range = node->forStmt.range, *vars = node->forStmt.var,
            *body = node->forStmt.body;
    // create the iterator
    const Type *rangeOperator =
                   findCustomRange(ctx, range->type, &range->loc, range->flags),
               *iterator = findStructMemberType(rangeOperator->func.retType,
                                                S_CallOverload);
    AstNode *rangeOperatorDecl = rangeOperator->func.decl;

    AstNode *rangeOperatorVar = makeVarDecl(
        ctx->pool,
        &range->loc,
        range->flags & ~(flgConst | flgTopLevelDecl),
        makeAnonymousVariable(ctx->strings, "_rg"),
        NULL,
        makeCallExpr(ctx->pool,
                     &range->loc,
                     makeMemberExpr(ctx->pool,
                                    &range->loc,
                                    range->flags,
                                    range,
                                    makeResolvedPath(ctx->pool,
                                                     &range->loc,
                                                     S_Range,
                                                     rangeOperatorDecl->flags,
                                                     rangeOperatorDecl,
                                                     NULL,
                                                     rangeOperatorDecl->type),
                                    NULL,
                                    rangeOperatorDecl->type),
                     NULL,
                     flgNone,
                     NULL,
                     rangeOperator->func.retType),
        NULL,
        rangeOperator->func.retType);
    astModifierAdd(&ctx->blockModifier, rangeOperatorVar);

    AstNode *callIterator =
        makeCallExpr(ctx->pool,
                     &range->loc,
                     makeResolvedPath(ctx->pool,
                                      &range->loc,
                                      rangeOperatorVar->varDecl.name,
                                      flgClosureStyle,
                                      rangeOperatorVar,
                                      NULL,
                                      NULL),
                     NULL,
                     flgNone,
                     NULL,
                     NULL);

    AstNode *loopVar = makeVarDecl(
        ctx->pool,
        &range->loc,
        flgNone,
        makeAnonymousVariable(ctx->strings, "_it"),
        makeTypeReferenceNode(ctx->pool, iterator->func.retType, &range->loc),
        deepCloneAstNode(ctx->pool, callIterator),
        NULL,
        NULL);
    astModifierAdd(&ctx->blockModifier, loopVar);

    // loopVar = iter()
    AstNode *update =
        makeExprStmt(ctx->pool,
                     &range->loc,
                     flgNone,
                     makeAssignExpr(ctx->pool,
                                    &range->loc,
                                    flgNone,
                                    makeResolvedPath(ctx->pool,
                                                     &range->loc,
                                                     loopVar->varDecl.name,
                                                     flgNone,
                                                     loopVar,
                                                     NULL,
                                                     loopVar->type),
                                    opAssign,
                                    callIterator,
                                    NULL,
                                    NULL),
                     NULL,
                     NULL);
    AstNode *condition =
        makeUnaryExpr(ctx->pool,
                      &range->loc,
                      flgNone,
                      true,
                      opNot,
                      makeUnaryExpr(ctx->pool,
                                    &range->loc,
                                    flgNone,
                                    true,
                                    opNot,
                                    makeResolvedPath(ctx->pool,
                                                     &range->loc,
                                                     loopVar->varDecl.name,
                                                     flgNone,
                                                     loopVar,
                                                     NULL,
                                                     loopVar->type),
                                    NULL,
                                    NULL),
                      NULL,
                      NULL);

    if (vars->next == NULL) {
        vars->varDecl.init =
            makeUnaryExpr(ctx->pool,
                          &range->loc,
                          flgNone,
                          true,
                          opDeref,
                          makeResolvedPath(ctx->pool,
                                           &range->loc,
                                           loopVar->varDecl.name,
                                           flgNone,
                                           loopVar,
                                           NULL,
                                           loopVar->type),
                          NULL,
                          NULL);
        vars->type = NULL;
        vars->next = body->blockStmt.stmts;
    }
    else {
        AstNode *newVars =
            makeVarDecl(ctx->pool,
                        &range->loc,
                        flgNone,
                        makeAnonymousVariable(ctx->strings, "tmpVar"),
                        NULL,
                        makeUnaryExpr(ctx->pool,
                                      &range->loc,
                                      flgNone,
                                      true,
                                      opDeref,
                                      makeResolvedPath(ctx->pool,
                                                       &range->loc,
                                                       loopVar->varDecl.name,
                                                       flgNone,
                                                       loopVar,
                                                       NULL,
                                                       loopVar->type),
                                      NULL,
                                      NULL),
                        NULL,
                        NULL);
        AstNode *var = vars, *it = newVars;

        for (i64 i = 0; var; i++) {
            if (isIgnoreVar(var->varDecl.names->ident.value)) {
                var = var->next;
                continue;
            }
            var->varDecl.init = makeMemberExpr(
                ctx->pool,
                &range->loc,
                flgNone,
                makeResolvedPath(ctx->pool,
                                 &range->loc,
                                 newVars->varDecl.name,
                                 flgNone,
                                 newVars,
                                 NULL,
                                 NULL),
                makeIntegerLiteral(ctx->pool, &range->loc, i, NULL, NULL),
                NULL,
                NULL);

            var->type = NULL;
            it->next = var;
            it = var;
            var = var->next;
        }

        it->next = body->blockStmt.stmts;
        vars = newVars;
    }

    clearAstBody(node);
    node->tag = astWhileStmt;
    node->whileStmt.cond = condition;
    body->blockStmt.stmts = vars;
    node->whileStmt.body = body;
    node->whileStmt.update =
        makeBlockStmt(ctx->pool, builtinLoc(), update, NULL, NULL);
    update->parentScope = body;
    body->parentScope = node;
    checkType(visitor, node);
}

void checkRangeExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *start = checkType(visitor, node->rangeExpr.start);
    if (typeIs(start, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }
    else if (!isNumericType(start)) {
        logError(ctx->L,
                 &node->rangeExpr.start->loc,
                 "`range` start type '{t}' is not supported, expecting a "
                 "numeric type",
                 (FormatArg[]){{.t = start}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    const Type *end = checkType(visitor, node->rangeExpr.end);
    if (!isTypeAssignableFrom(start, end)) {
        logError(ctx->L,
                 &node->rangeExpr.end->loc,
                 "`range` inconsistent types, expect type `{t}`, but got `{t}`",
                 (FormatArg[]){{.t = start}, {.t = end}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (node->rangeExpr.step) {
        const Type *step = checkType(visitor, node->rangeExpr.end);
        if (!isNumericType(step)) {
            logError(ctx->L,
                     &node->rangeExpr.start->loc,
                     "`range` step type '{t}' is not supported, expecting a "
                     "numeric type",
                     (FormatArg[]){{.t = start}, {.t = end}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
    }
    node->type = start;
}

void checkForStmt(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *variable = node->forStmt.var;
    u64 numVariables = countAstNodes(variable);

    const Type *range = checkType(visitor, node->forStmt.range);
    if (typeIs(range, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    const Type *range_ = stripAll(range);
    if (nodeIs(node->forStmt.range, RangeExpr)) {
        if (numVariables != 1) {
            logError(ctx->L,
                     &node->forStmt.var->loc,
                     "unexpected number of for statement variables, expecting "
                     "1, got {u64}",
                     (FormatArg[]){{.u64 = numVariables}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
        variable->type = range_;
    }
    else if (typeIs(range_, Array)) {
        if (numVariables > 2) {
            logError(
                ctx->L,
                &node->forStmt.var->loc,
                "unexpected number of `for` statement variables, expecting "
                "at most 2, got {u64}",
                (FormatArg[]){{.u64 = numVariables}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
        variable->type = range_->array.elementType;
        if (variable->next)
            variable->next->type = getPrimitiveType(ctx->types, prtI64);
    }
    else if (typeIs(range_, String)) {
        if (numVariables > 2) {
            logError(
                ctx->L,
                &node->forStmt.var->loc,
                "unexpected number of `for` statement variables, expecting "
                "at most 2, got {u64}",
                (FormatArg[]){{.u64 = numVariables}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
        variable->type = getPrimitiveType(ctx->types, prtCChar);
        if (variable->next)
            variable->next->type = getPrimitiveType(ctx->types, prtI64);
    }
    else if (isStructType(range_) || isClassType(range_)) {
        if (!checkForStmtCustomRange(ctx, node, numVariables, range_)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }
        transformForCustomRange(visitor, node, numVariables);
        return;
    }
    else {
        logError(ctx->L,
                 &node->forStmt.range->loc,
                 "unexpected range expression, range of type `{t}` cannot be "
                 "enumerated",
                 (FormatArg[]){{.t = range_}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    bool currentReturnState = ctx->returnState;
    ctx->returnState = false;
    node->type = checkType(visitor, node->forStmt.body);
    ctx->returnState = currentReturnState;
}
