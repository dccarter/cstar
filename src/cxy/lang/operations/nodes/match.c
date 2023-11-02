//
// Created by Carter Mbotho on 2023-11-01.
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

#include <string.h>

static inline bool isSameCase(const AstNode *lhs, const AstNode *rhs)
{
    if (lhs->caseStmt.match == rhs->caseStmt.match)
        return true;
    if ((lhs->caseStmt.match == NULL) || (rhs->caseStmt.match == NULL))
        return false;
    return lhs->caseStmt.match->type == rhs->caseStmt.match->type;
}

void generateMatchStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getAstVisitorContext(visitor);
    format(ctx->state, "switch (", NULL);
    astConstVisit(visitor, node->matchStmt.expr);
    format(ctx->state,
           "{s}tag) {{{>}\n",
           (FormatArg[]){
               {.s = isPointerType(node->matchStmt.expr->type) ? "->" : "."}});

    const AstNode *case_ = node->matchStmt.cases;
    for (u32 i = 0; case_; case_ = case_->next, i++) {
        astConstVisit(visitor, case_);
        if (case_->next)
            format(ctx->state, "\n", NULL);
    }
    format(ctx->state, "{<}\n}", NULL);
}

void generateMatchCaseStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getAstVisitorContext(visitor);
    AstNode *cond = node->parentScope->matchStmt.expr;

    if (node->caseStmt.match == NULL)
        format(ctx->state, "default:", NULL);
    else
        format(ctx->state,
               "case {u32}:",
               (FormatArg[]){{.u32 = node->caseStmt.idx}});

    if (node->caseStmt.body == NULL) {
        if (node->caseStmt.match == NULL)
            format(ctx->state, "\nbreak;", NULL);
        return;
    }

    if (!nodeIs(node->caseStmt.body, BlockStmt) || node->caseStmt.variable)
        format(ctx->state, "{{{>}\n", NULL);

    if (node->caseStmt.variable) {
        generateTypeUsage(ctx, node->caseStmt.variable->type);
        format(ctx->state,
               " {s} = ",
               (FormatArg[]){{.s = node->caseStmt.variable->ident.value}});
        if (hasFlag(node->caseStmt.variable, Reference))
            format(ctx->state, "&", NULL);
        astConstVisit(visitor, cond);
        format(ctx->state,
               "{s}_{u32};\n",
               (FormatArg[]){{.s = isPointerType(cond->type) ? "->" : "."},
                             {.u32 = node->caseStmt.idx}});
    }

    astConstVisit(visitor, node->caseStmt.body);

    if (!nodeIs(node->caseStmt.body, BlockStmt) || node->caseStmt.variable)
        format(ctx->state, "{<}\n}\nbreak;", NULL);
    else if (node->caseStmt.body || node->caseStmt.match == NULL)
        format(ctx->state, "\nbreak;", NULL);
}

void checkMatchCaseStmt(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *match = node->caseStmt.match, *body = node->caseStmt.body;
    if (match) {
        const Type *match_ = checkType(visitor, match);
        if (typeIs(match_, Error)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }

        u64 flags = flgNone;
        const Type *sumType =
            stripOnce(node->parentScope->matchStmt.expr->type, &flags);
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
            node->caseStmt.variable->flags |= (flags & flgConst);
            if (hasFlag(node->caseStmt.variable, Reference))
                node->caseStmt.variable->type =
                    makePointerType(ctx->types,
                                    match_,
                                    node->caseStmt.variable->flags & flgConst);
            else
                node->caseStmt.variable->type = match_;
        }
    }

    if (body) {
        if (nodeIs(body, BlockStmt))
            body->flags |= flgBlockReturns;
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
    const Type *unwrapped = stripOnce(expr_, &flags);
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
    for (; case_; case_ = case_->next) {
        if (hasFlag(case_, Comptime)) {
            if (!evaluate(ctx->evaluator, case_)) {
                node->type = ERROR_TYPE(ctx);
                continue;
            }
        }
        const Type *type = case_->type ?: checkType(visitor, case_);
        if (typeIs(type, Error)) {
            node->type = ERROR_TYPE(ctx);
        }
    }
    if (typeIs(node->type, Error))
        return;

    // verify correctness of the match statement
    cases = node->matchStmt.cases, case_ = cases;
    AstNode *first = cases, *it = NULL;
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

    if (typeIs(node->type, Error)) {
        free(types);
        return;
    }
    free(types);
}