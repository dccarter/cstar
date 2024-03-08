//
// Created by Carter on 2023-08-30.
//
#include "check.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"

#include "core/alloc.h"
#include "lang/middle/eval/eval.h"

#include <string.h>

typedef struct {
    TypingContext *ctx;
    AstNode *node;
} NodeComparisonContext;

#define cxy_compare(X, Y)                                                      \
    ({                                                                         \
        __typeof(X) LINE_VAR(Xx) = (X);                                        \
        __typeof(Y) LINE_VAR(Yy) = (Y);                                        \
        (LINE_VAR(Xx) < LINE_VAR(Yy) ? -1                                      \
                                     : (LINE_VAR(Xx) > LINE_VAR(Yy) ? 1 : 0)); \
    })

static i64 getEnumValue(const AstNode *node)
{
    csAssert0(typeIs(node->type, Enum));
    cstring name = NULL;
    if (nodeIs(node, Path) && node->path.elements->next)
        name = node->path.elements->next->pathElement.name;
    else if (nodeIs(node, MemberExpr) &&
             nodeIs(node->memberExpr.member, Identifier))
        name = node->memberExpr.member->ident.value;
    else
        unreachable("UNSUPPORTED");

    const EnumOptionDecl *option = findEnumOption(node->type, name);
    csAssert0(option);

    return option->value;
}

static i64 getLiteralValue(const AstNode *node)
{
    if (typeIs(node->type, Enum))
        return getEnumValue(node);
    return integerLiteralValue(node);
}

static int isIntegerInRange(const AstNode *range, i64 value)
{
    i64 min = getLiteralValue(range->rangeExpr.start),
        max = getLiteralValue(range->rangeExpr.end);
    if (value < min)
        return -1;
    if (value > max)
        return 1;
    return 0;
}

static int isOverlappingRange(const AstNode *lhs, const AstNode *rhs)
{
    i64 minLhs = getLiteralValue(lhs->rangeExpr.start),
        maxLhs = getLiteralValue(lhs->rangeExpr.end),
        minRhs = getLiteralValue(rhs->rangeExpr.start),
        maxRhs = getLiteralValue(rhs->rangeExpr.end);
    if (maxLhs < minRhs)
        return -1;
    if (minLhs > maxRhs)
        return 1;

    return 0;
}

static int compareLiteralNode(const void *left, const void *right)
{
    const NodeComparisonContext *compare = right;
    TypingContext *ctx = compare->ctx;
    const AstNode *lhs = ((NodeComparisonContext *)left)->node;
    const AstNode *rhs = compare->node;

    if (nodeIs(lhs, RangeExpr) && nodeIs(rhs, RangeExpr)) {
        return isOverlappingRange(lhs, rhs);
    }
    else if (nodeIs(lhs, RangeExpr)) {
        csAssert0(isIntegralLiteral(rhs));
        return isIntegerInRange(lhs, getLiteralValue(rhs));
    }
    else if (nodeIs(rhs, RangeExpr)) {
        csAssert0(isIntegralLiteral(lhs));
        return -isIntegerInRange(rhs, getLiteralValue(lhs));
    }

    csAssert0(isLiteralExpr(lhs));
    csAssert0(isLiteralExpr(rhs));

    switch (lhs->tag) {
    case astIntegerLit:
        return cxy_compare(getLiteralValue(lhs), getLiteralValue(rhs));
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
        return cxy_compare(getEnumValue(lhs), getEnumValue(rhs));
    }
}

void checkDuplicateCaseMatches(TypingContext *ctx,
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

void checkCaseStmt(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    if (nodeIs(node->parentScope, MatchStmt)) {
        checkMatchCaseStmt(visitor, node);
        return;
    }

    const Type *cond =
        getParentScopeWithTag(node, astSwitchStmt)->switchStmt.cond->type;

    if (node->caseStmt.match) {
        const Type *match = checkType(visitor, node->caseStmt.match);
        if (!isTypeAssignableFrom(match, cond)) {
            logError(
                ctx->L,
                &node->caseStmt.match->loc,
                "switch case statement match of type '{t}' incompatible with "
                "switch pattern type '{t}'",
                (FormatArg[]){{.t = match}, {.t = cond}});
            node->type = ERROR_TYPE(ctx);
            return;
        }

        if (nodeIs(node->caseStmt.match, RangeExpr)) {
            AstNode *min = node->caseStmt.match->rangeExpr.start,
                    *max = node->caseStmt.match->rangeExpr.end;
            if (!isIntegralLiteral(min) || !isIntegralLiteral(max)) {
                logError(ctx->L,
                         &node->caseStmt.match->loc,
                         "switch case statement match of type '{t}' must be a "
                         "compile time literal expression",
                         (FormatArg[]){{.t = match}});
                node->type = ERROR_TYPE(ctx);
                return;
            }

            i64 iMin = getLiteralValue(min), iMax = getLiteralValue(max);
            if (!isIntegerTypeInRange(cond, iMin, iMax)) {
                logError(
                    ctx->L,
                    &node->caseStmt.match->loc,
                    "switch case statement range values will never match any "
                    "value of condition type, ({i64} - {i64}) is out of '{t}' "
                    "values range",
                    (FormatArg[]){{.i64 = iMin}, {.i64 = iMax}, {.t = cond}});
                node->type = ERROR_TYPE(ctx);
                return;
            }
        }
        else if (isIntegralLiteral(node->caseStmt.match)) {
            i64 value = getLiteralValue(node->caseStmt.match);
            if (!isIntegerTypeInRange(cond, value, value)) {
                logError(
                    ctx->L,
                    &node->caseStmt.match->loc,
                    "switch case statement integral value will never match any "
                    "value of condition type, {i64} is out of '{t}' "
                    "values range",
                    (FormatArg[]){{.i64 = value}, {.t = cond}});
                node->type = ERROR_TYPE(ctx);
                return;
            }
        }
        else if (!nodeIs(node->caseStmt.match, StringLit)) {
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
        node->type = checkType(visitor, node->caseStmt.body);
    else
        node->type = makeVoidType(ctx->types);
}

void checkSwitchStmt(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *match = checkType(visitor, node->switchStmt.cond),
               *match_ = unwrapType(match, NULL);
    if (!isIntegralType(match_) && !typeIs(match_, String)) {
        logError(ctx->L,
                 &node->switchStmt.cond->loc,
                 "switch match expression type must be an integral or string "
                 "type, got '{t}'",
                 (FormatArg[]){{.t = match}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    AstNode *case_ = node->switchStmt.cases;
    // On the first pass we just check types
    for (; case_; case_ = case_->next) {
        case_->parentScope = node;
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

    u64 count = countAstNodes(node->switchStmt.cases);
    NodeComparisonContext *matches =
        mallocOrDie(sizeof(NodeComparisonContext) * count);

    const Type *type = NULL;
    case_ = node->matchStmt.cases;
    u64 i = 0;
    for (; case_; case_ = case_->next) {
        case_->parentScope = node;
        type = case_->type;

        if (case_->caseStmt.match)
            matches[i++] = (NodeComparisonContext){
                .node = case_->caseStmt.match, .ctx = ctx};
    }

    if (typeIs(node->type, Error)) {
        free(matches);
        return;
    }
    node->type = type;

    qsort(matches, i, sizeof(NodeComparisonContext), compareLiteralNode);
    checkDuplicateCaseMatches(ctx, node, matches, i);
    free(matches);
}
