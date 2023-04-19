//
// Created by Carter on 2023-04-18.
//

#include "lang/codegen.h"
#include "lang/semantics.h"

#include "lang/ttable.h"

void generateForStmtRange(ConstAstVisitor *visitor, const AstNode *node)
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

void checkRangeExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *i64Type = getPrimitiveType(ctx->typeTable, prtI64);

    const Type *start = evalType(visitor, node->rangeExpr.start);
    const Type *end = evalType(visitor, node->rangeExpr.end);

    if (!isTypeAssignableFrom(i64Type, start)) {
        logError(ctx->L,
                 &node->rangeExpr.start->loc,
                 "expecting integer type for range start expression, got '{t}'",
                 (FormatArg[]){{.t = start}});
        start = ERROR_TYPE(ctx);
    }

    if (!isTypeAssignableFrom(i64Type, end)) {
        logError(ctx->L,
                 &node->rangeExpr.start->loc,
                 "expecting integer type for range end expression, got '{t}'",
                 (FormatArg[]){{.t = end}});
        start = ERROR_TYPE(ctx);
    }

    if (node->rangeExpr.step) {
        const Type *step = evalType(visitor, node->rangeExpr.step);
        if (!isTypeAssignableFrom(i64Type, end)) {
            logError(
                ctx->L,
                &node->rangeExpr.start->loc,
                "expecting integer type for range step expression, got '{t}'",
                (FormatArg[]){{.t = step}});
            start = ERROR_TYPE(ctx);
        }
    }

    node->type = makeOpaqueType(ctx->typeTable, "cxy_range_t");
}