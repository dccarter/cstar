//
// Created by Carter Mbotho on 2023-11-01.
//

#include "check.h"

#include "../eval/eval.h"

#include "lang/frontend/flag.h"

#include "core/alloc.h"

static inline bool isSameCase(const AstNode *lhs, const AstNode *rhs)
{
    if (lhs->caseStmt.match == rhs->caseStmt.match)
        return true;
    if ((lhs->caseStmt.match == NULL) || (rhs->caseStmt.match == NULL))
        return false;
    return lhs->caseStmt.match->type == rhs->caseStmt.match->type;
}

void checkMatchCaseStmt(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *match = node->caseStmt.match, *body = node->caseStmt.body;
    if (match) {
        AstNode *condition = node->parentScope->matchStmt.expr;
        const Type *match_ = checkType(visitor, match);
        if (typeIs(match_, Error)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }

        u64 flags = flgNone;
        const Type *sumType =
            stripPointerOrReferenceOnce(condition->type, &flags);
        node->caseStmt.idx = findUnionTypeIndex(sumType, match_);
        if (node->caseStmt.idx == UINT32_MAX) {
            logError(ctx->L,
                     &match->loc,
                     "type {t} is not a union member of '{t}'",
                     (FormatArg[]){{.t = match_}, {.t = sumType}});
            node->type = ERROR_TYPE(ctx);
            return;
        }

        if (node->caseStmt.variable) {
            AstNode *variable = node->caseStmt.variable;
            const Type *type = node->caseStmt.match->type;
            if (isComplexType(type) || hasFlag(variable, Reference))
                type = makeReferenceType(
                    ctx->types, type, (condition->flags & flgConst));
            node->caseStmt.variable->type = type;
        }
    }
    else {
        node->parentScope->matchStmt.defaultCase = node;
    }

    if (body) {
        node->type = checkType(visitor, body);
    }
    else
        node->type = makeVoidType(ctx->types);
}

void checkMatchStmt(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->matchStmt.expr, *cases = node->matchStmt.cases;

    const Type *expr_ = checkType(visitor, node->matchStmt.expr);
    if (typeIs(expr_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    u64 flags = flgNone;
    const Type *unwrapped = stripPointerOrReferenceOnce(expr_, &flags);
    if (!typeIs(unwrapped, Union)) {
        logError(ctx->L,
                 &expr->loc,
                 "expression of type '{t}' cannot be used on a match "
                 "statement, expecting a union typed expression",
                 (FormatArg[]){{.t = unwrapped}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    // On the first pass we just check types
    AstNode *case_ = cases;
    bool returnState = false, currentReturnState = ctx->returnState;
    for (; case_; case_ = case_->next) {
        ctx->returnState = false;
        if (hasFlag(case_, Comptime)) {
            if (!evaluate(ctx->evaluator, case_)) {
                node->type = ERROR_TYPE(ctx);
                returnState = ctx->returnState && returnState;
                continue;
            }
        }
        const Type *type = case_->type ?: checkType(visitor, case_);
        returnState = ctx->returnState && returnState;
        if (typeIs(type, Error)) {
            node->type = ERROR_TYPE(ctx);
        }
    }
    ctx->returnState = returnState || currentReturnState;
    if (typeIs(node->type, Error))
        return;

    // verify correctness of the match statement
    cases = node->matchStmt.cases, case_ = cases;
    AstNode *it = NULL;
    u64 count = countAstNodes(cases);

    const Type **types = mallocOrDie(sizeof(Type *) * count);
    for (u64 i = 0; case_; case_ = case_->next, i++) {
        for (it = cases; it != case_; it = it->next) {
            if (isSameCase(it, case_)) {
                logError(ctx->L,
                         &case_->loc,
                         "duplicate case type '{t}' in match statement",
                         (FormatArg[]){{.t = it->type}});
                logNote(ctx->L,
                        &it->loc,
                        "case for type '{t}' already defined here",
                        (FormatArg[]){{.t = it->type}});
                node->type = ERROR_TYPE(ctx);
                break;
            }
            types[i] = case_->type;
        }
    }

    free(types);
}
