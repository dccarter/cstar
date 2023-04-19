//
// Created by Carter on 2023-04-18.
//

#include "lang/codegen.h"
#include "lang/semantics.h"

#include "lang/ttable.h"

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

static const Type *checkNewInitializerExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    if (isSupportedOnAuto(node))
        return evalType(visitor, node);

    return NULL;
}

void generateNewExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const char *name = makeAnonymousVariable((ctx)->strPool, "cxy_new_temp");
    const Type *type = node->type->pointer.pointed;

    format(ctx->state, "({{{>}\n", NULL);
    generateTypeUsage(ctx, node->type);
    format(ctx->state, " {s} = cxy_alloc(sizeof(", (FormatArg[]){{.s = name}});
    generateTypeUsage(ctx, type);
    format(ctx->state, "));\n", NULL);
    if (node->newExpr.init) {
        if (type->tag == typArray) {
            format(ctx->state, " memcpy(*{s}, &(", (FormatArg[]){{.s = name}});
            generateTypeUsage(ctx, type);
            format(ctx->state, ")", NULL);
            astConstVisit(visitor, node->newExpr.init);
            format(ctx->state, ", sizeof(", NULL);
            generateTypeUsage(ctx, type);
            format(ctx->state, "))", NULL);
        }
        else {
            format(ctx->state, " *{s} = ", (FormatArg[]){{.s = name}});
            astConstVisit(visitor, node->newExpr.init);
        }
        format(ctx->state, ";\n", NULL);
    }
    format(ctx->state, " {s};{<}\n})", (FormatArg[]){{.s = name}});
}

void checkNewExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *type = NULL, *init = NULL;
    node->flags |= flgNewAllocated;

    if (node->newExpr.type)
        type = evalType(visitor, node->newExpr.type);

    if (node->newExpr.init) {
        init = checkNewInitializerExpr(visitor, node->newExpr.init);
        if (init == NULL) {
            logError(ctx->L,
                     &node->loc,
                     "`new` operator syntax not supported",
                     NULL);
            node->type = ERROR_TYPE(ctx);
            return;
        }
    }

    if (type == NULL) {
        type = init;
    }

    if (init && !isTypeAssignableFrom(type, init)) {
        logError(
            ctx->L,
            &node->loc,
            "new initializer value type '{t}' is not assignable to type '{t}'",
            (FormatArg[]){{.t = type}, {.t = init}});
    }
    node->flags = (node->newExpr.type ? node->newExpr.type->flags
                                      : node->newExpr.init->flags);
    node->type =
        makePointerType(ctx->typeTable, type, type->flags | flgNewAllocated);
}
