/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-26
 */

#include "lang/codegen.h"
#include "lang/semantics.h"

#include "lang/ttable.h"

#include "core/alloc.h"

#include <string.h>

typedef struct {
    SemanticsContext *ctx;
    AstNode *node;
} NodeComparisonContext;

#define cxy_compare(X, Y) ((X) < (Y) ? -1 : ((X) > (Y) ? 1 : 0))

const i64 *getEnumValue(SemanticsContext *ctx, const AstNode *node)
{
    AstNode *symbol = findSymbolOnlyByNode(&ctx->env, node);
    csAssert0(symbol && nodeIs(symbol, EnumOption));

    return &symbol->enumOption.value->intLiteral.value;
}

static int compareLiteralNode(const void *left, const void *right)
{
    const NodeComparisonContext *compare = right;
    SemanticsContext *ctx = compare->ctx;
    const AstNode *lhs = ((NodeComparisonContext *)left)->node;
    const AstNode *rhs = compare->node;

    csAssert0(isLiteralExpr(lhs));
    csAssert0(isLiteralExpr(rhs));

    switch (lhs->tag) {
    case astIntegerLit:
        return cxy_compare(lhs->intLiteral.value, rhs->intLiteral.value);
    case astFloatLit:
        return cxy_compare(lhs->floatLiteral.value, rhs->floatLiteral.value);
    case astBoolLit:
        return cxy_compare(lhs->boolLiteral.value, rhs->boolLiteral.value);
    case astStringLit:
        return strcmp(lhs->stringLiteral.value, rhs->stringLiteral.value);
    case astCharLit:
        return cxy_compare(lhs->charLiteral.value, rhs->charLiteral.value);
    default:
        csAssert0(typeIs(lhs->type, Enum) && lhs->type == rhs->type &&
                  isEnumLiteral(rhs));
        return cxy_compare(*getEnumValue(ctx, lhs), *getEnumValue(ctx, rhs));
    }
}

void checkDuplicateCaseMatches(SemanticsContext *ctx,
                               AstNode *node,
                               NodeComparisonContext *cases,
                               u64 count)
{
    if (count == 0)
        return;

    NodeComparisonContext *prev = &cases[0];
    for (u64 i = 1; i < count; i++) {
        if (compareLiteralNode(prev, &cases[i]) == 0) {
            logError(ctx->L,
                     &cases[i].node->loc,
                     "duplicate case match on switch statement prohibited",
                     NULL);
            logNote(ctx->L,
                    &prev->node->loc,
                    "previous similar match statement here",
                    NULL);
            cases[i].node->type = ERROR_TYPE(ctx);
            node->type = cases[i].node->type;
        }
        prev = &cases[i];
    }
}

void generateCaseStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getAstVisitorContext(visitor);
    AstNode *cond = node->parentScope->switchStmt.cond;
    u64 defaultTag = node->parentScope->switchStmt.index;

    if (node->caseStmt.match) {
        const AstNode *match = node->caseStmt.match;
        format(ctx->state, "case ", NULL);
        if (typeIs(match->type, String)) {
            format(ctx->state,
                   "{i64}:\n",
                   (FormatArg[]){{.i64 = hashStr(hashInit(),
                                                 match->stringLiteral.value)}});
            format(ctx->state, "if (strcmp(", NULL);
            astConstVisit(visitor, cond);
            format(ctx->state,
                   ", \"{s}\") != 0) goto __switch_default_{u64};",
                   (FormatArg[]){{.s = match->stringLiteral.value},
                                 {.u64 = defaultTag}});
        }
        else {
            astConstVisit(visitor, node->caseStmt.match);
            format(ctx->state, ":", NULL);
        }
    }
    else {
        format(ctx->state, "default:", NULL);
        if (typeIs(cond->type, String))
            format(ctx->state,
                   "\n__switch_default_{u64}:",
                   (FormatArg[]){{.u64 = defaultTag}});
    }

    if (node->caseStmt.body == NULL) {
        format(ctx->state, "\n", NULL);
        return;
    }

    if (!nodeIs(node->caseStmt.body, BlockStmt))
        format(ctx->state, "{>}\n", NULL);

    astConstVisit(visitor, node->caseStmt.body);

    if (!nodeIs(node->caseStmt.body, BlockStmt))
        format(ctx->state, "{<}", NULL);
}

void generateSwitchStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getAstVisitorContext(visitor);
    format(ctx->state, "switch (", NULL);
    bool isStringMatch = typeIs(node->switchStmt.cond->type, String);
    if (isStringMatch)
        format(ctx->state, "cxy_hash_string(cxy_hash_init(), ", NULL);
    astConstVisit(visitor, node->switchStmt.cond);

    if (isStringMatch)
        format(ctx->state, ")", NULL);

    format(ctx->state, ") {{{>}\n", NULL);

    const AstNode *case_ = node->switchStmt.cases;
    bool hasDefault = false;
    for (; case_; case_ = case_->next) {
        astConstVisit(visitor, case_);
        if (case_->next)
            format(ctx->state, "\n", NULL);
        hasDefault = case_->caseStmt.match == NULL;
    }
    if (!hasDefault && isStringMatch) {
        format(ctx->state,
               "\ndefault:\n__switch_default_{u64}: break;",
               (FormatArg[]){{.u64 = node->switchStmt.index}});
    }
    format(ctx->state, "{<}\n}", NULL);
}

void checkCaseStmt(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *cond = node->parentScope->switchStmt.cond->type;

    if (node->caseStmt.match) {
        const Type *match = evalType(visitor, node->caseStmt.match);
        if (!isTypeAssignableFrom(cond, match)) {
            logError(
                ctx->L,
                &node->caseStmt.match->loc,
                "switch case statement match of type '{t}' incompatible with "
                "switch pattern type '{t}'",
                (FormatArg[]){{.t = match}, {.t = cond}});
            node->type = ERROR_TYPE(ctx);
            return;
        }

        if (!isIntegralLiteral(node->caseStmt.match) &&
            !nodeIs(node->caseStmt.match, StringLit)) {
            logError(ctx->L,
                     &node->caseStmt.match->loc,
                     "switch case statement match of type '{t}' must be a "
                     "compile time literal expression",
                     (FormatArg[]){{.t = match}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
    }

    if (node->caseStmt.body)
        node->type = evalType(visitor, node->caseStmt.body);
    else
        node->type = makeVoidType(ctx->typeTable);
}

void checkSwitchStmt(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *match = evalType(visitor, node->switchStmt.cond);
    if (!isIntegralType(match) && !typeIs(match, String)) {
        logError(
            ctx->L,
            &node->switchStmt.cond->loc,
            "switch match expression type must be an integral type, got '{t}'",
            (FormatArg[]){{.t = match}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    pushScope(&ctx->env, node);
    AstNode *case_ = node->switchStmt.cases;
    u64 count = countAstNodes(node->switchStmt.cases);
    NodeComparisonContext *matches =
        mallocOrDie(sizeof(NodeComparisonContext) * count);

    const Type *type, *prevNonVoid = NULL;
    u64 i = 0;
    for (; case_; case_ = case_->next) {
        case_->parentScope = node;
        type = evalType(visitor, case_);
        if (typeIs(type, Error)) {
            node->type = type;
            continue;
        }

        if (case_->caseStmt.match)
            matches[i++] = (NodeComparisonContext){
                .node = case_->caseStmt.match, .ctx = ctx};
    }
    popScope(&ctx->env);
    if (typeIs(node->type, Error)) {
        free(matches);
        return;
    }
    node->type = type;

    qsort(matches, i, sizeof(NodeComparisonContext), compareLiteralNode);
    checkDuplicateCaseMatches(ctx, node, matches, i);
    free(matches);
}