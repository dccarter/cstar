//
// Created by Carter on 2023-08-30.
//
#include "../check.h"
#include "../codegen.h"

#include "lang/capture.h"
#include "lang/flag.h"
#include "lang/operations.h"
#include "lang/strings.h"
#include "lang/ttable.h"
#include "lang/visitor.h"

#include "core/alloc.h"

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

    const EnumOption *option = findEnumOption(node->type, name);
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

void generateCaseStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getAstVisitorContext(visitor);
    if (nodeIs(node->parentScope, MatchStmt)) {
        generateMatchCaseStmt(visitor, node);
        return;
    }

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
                   ", \"{s}\") != 0) goto CXY__switch_default_{u64};",
                   (FormatArg[]){{.s = match->stringLiteral.value},
                                 {.u64 = defaultTag}});
        }
        else if (nodeIs(node->caseStmt.match, RangeExpr)) {
            AstNode *range = node->caseStmt.match;
            astConstVisit(visitor, range->rangeExpr.start);
            format(ctx->state, " ... ", NULL);
            astConstVisit(visitor, range->rangeExpr.end);
            format(ctx->state, ":", NULL);
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
                   "\nCXY__switch_default_{u64}:",
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
        format(ctx->state, "break;\n{<}", NULL);
    else if (node->caseStmt.body)
        format(ctx->state, "\nbreak;", NULL);
}

void generateSwitchStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getAstVisitorContext(visitor);
    format(ctx->state, "switch (", NULL);
    bool isStringMatch = typeIs(node->switchStmt.cond->type, String);
    if (isStringMatch)
        format(ctx->state,
               "CXY__builtins_fnv1a_string(CXY__builtins_fnv1a_init(), ",
               NULL);
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
               "\ndefault:\nCXY__switch_default_{u64}: break;",
               (FormatArg[]){{.u64 = node->switchStmt.index}});
    }
    format(ctx->state, "{<}\n}", NULL);
}

void checkCaseStmt(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    if (nodeIs(node->parentScope, MatchStmt)) {
        checkMatchCaseStmt(visitor, node);
        return;
    }

    const Type *cond = node->parentScope->switchStmt.cond->type;

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
    const Type *match = checkType(visitor, node->switchStmt.cond);
    if (!isIntegralType(match) && !typeIs(match, String)) {
        logError(ctx->L,
                 &node->switchStmt.cond->loc,
                 "switch match expression type must be an integral or string "
                 "type, got '{t}'",
                 (FormatArg[]){{.t = match}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    AstNode *case_ = node->switchStmt.cases;
    u64 count = countAstNodes(node->switchStmt.cases);
    NodeComparisonContext *matches =
        mallocOrDie(sizeof(NodeComparisonContext) * count);

    const Type *type, *prevNonVoid = NULL;
    u64 i = 0;
    for (; case_; case_ = case_->next) {
        case_->parentScope = node;
        type = checkType(visitor, case_);
        if (typeIs(type, Error)) {
            node->type = type;
            continue;
        }

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
