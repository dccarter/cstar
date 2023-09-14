//
// Created by Carter on 2023-08-27.
//

#include "../check.h"
#include "../codegen.h"
#include "../eval.h"

#include "lang/capture.h"
#include "lang/flag.h"
#include "lang/operations.h"
#include "lang/strings.h"
#include "lang/ttable.h"
#include "lang/visitor.h"

#include "core/alloc.h"

static bool checkForStmtStructRange(TypingContext *ctx,
                                    AstNode *node,
                                    u64 variablesCount,
                                    const Type *range)
{
    AstNode *variable = node->forStmt.var;
    const Type *rangeOp = findStructMemberType(range, S_Range);
    if (rangeOp) {
        rangeOp =
            matchOverloadedFunction(ctx,
                                    rangeOp,
                                    (const Type *[]){},
                                    0,
                                    &node->forStmt.range->loc,
                                    node->forStmt.range->flags & flgConst);
    }

    if (rangeOp == NULL) {
        logError(ctx->L,
                 &node->loc,
                 "type `{t}` does not have a range overload operator",
                 (FormatArg[]){{.t = range}});
        return false;
    }

    csAssert0(typeIs(rangeOp, Func) && typeIs(rangeOp->func.retType, Struct) &&
              hasFlag(rangeOp->func.retType, Closure));

    const Type *iterator =
        findStructMemberType(rangeOp->func.retType, S_CallOverload);
    csAssert0(typeIs(iterator, Func));

    const Type *value = iterator->func.retType->optional.target;
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

static void generateForStmtRange(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *var = node->forStmt.var;
    const AstNode *range = node->forStmt.range;

    format(ctx->state, "for (", NULL);
    generateTypeUsage(ctx, var->type);
    format(ctx->state, " ", NULL);
    astConstVisit(visitor, var->varDecl.names);
    format(ctx->state, " = ", NULL);
    astConstVisit(visitor, range->rangeExpr.start);
    format(ctx->state, "; ", NULL);
    astConstVisit(visitor, var->varDecl.names);
    format(ctx->state, " < ", NULL);
    astConstVisit(visitor, range->rangeExpr.end);
    format(ctx->state, "; ", NULL);
    astConstVisit(visitor, var->varDecl.names);
    if (range->rangeExpr.step) {
        format(ctx->state, " += ", NULL);
        astConstVisit(visitor, range->rangeExpr.step);
    }
    else
        format(ctx->state, "++", NULL);

    format(ctx->state, ") ", NULL);
    astConstVisit(visitor, node->forStmt.body);
}

void generateForStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    const AstNode *range = node->forStmt.range;
    const Type *range_ = unwrapType(range->type, NULL);

    if (range->tag == astRangeExpr) {
        generateForStmtRange(visitor, node);
    }
    else if (typeIs(range_, Array)) {
        generateForStmtArray(visitor, node);
    }
    else {
        unreachable("currently not supported");
    }
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
        variable->type = getPrimitiveType(ctx->types, prtChar);
        if (variable->next)
            variable->next->type = getPrimitiveType(ctx->types, prtI64);
    }
    else if (typeIs(range_, Struct)) {
        if (!checkForStmtStructRange(ctx, node, numVariables, range_)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }
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

    node->type = checkType(visitor, node->forStmt.body);
}

void evalForStmt(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    FileLoc rangeLoc = node->forStmt.range->loc;

    if (!evaluate(visitor, node->forStmt.range)) {
        node->tag = astError;
        return;
    }

    AstNode *range = node->forStmt.range;
    if (!hasFlag(range, ComptimeIterable)) {
        logError(ctx->L,
                 &rangeLoc,
                 "`#for` loop range expression is not comptime iterable",
                 NULL);
        node->tag = astError;
        return;
    }

    AstNode *it = nodeIs(range, ComptimeOnly) ? range->next : range;
    // open a new scope
    AstNodeList nodes = {NULL};
    AstNode *variable = node->forStmt.var;

    while (it) {
        AstNode *body = shallowCloneAstNode(ctx->pool, node->forStmt.body);
        variable->varDecl.init = it;
        if (!evaluate(visitor, body) || nodeIs(body, Error)) {
            node->tag = astError;
            return;
        }

        if (!nodeIs(it, Nop))
            insertAstNode(&nodes, body);

        it = it->next;
    }

    if (nodes.first == NULL) {
        node->tag = astNop;
    }
    else {
        nodes.last->next = node->next;
        *node = *nodes.first;
    }
}
