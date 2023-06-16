//
// Created by Carter on 2023-04-18.
//

#include "lang/codegen.h"
#include "lang/semantics.h"

#include "lang/ttable.h"

#include "core/alloc.h"

static void spreadTupleExpr(AstVisitor *visitor,
                            const Type *operand,
                            AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->unaryExpr.operand;
    AstNode *parts = NULL, *part = NULL;

    for (i64 i = 0; i < operand->tuple.count; i++) {
        const Type *type = operand->tuple.members[i];
        AstNode *next = makeAstNode(
            ctx->pool,
            &node->loc,
            &(AstNode){.tag = astMemberExpr,
                       .flags = expr->flags,
                       .memberExpr = {.target = cloneAstNode(ctx->pool, expr),
                                      .member = makeAstNode(
                                          ctx->pool,
                                          &node->loc,
                                          &(AstNode){.flags = type->flags,
                                                     .type = type,
                                                     .tag = astIntegerLit,
                                                     .intLiteral.value = i})}});
        type = evalType(visitor, next);
        if (typeIs(type, Error)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }

        if (parts) {
            part->next = next;
            part = part->next;
        }
        else {
            parts = part = next;
        }
    }

    replaceAstNode(node, parts);
}

static const Type *checkPrefixExpr(AstVisitor *visitor,
                                   const Type *operand,
                                   AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    switch (node->unaryExpr.op) {
    case opPreDec:
    case opPreInc:
        if (node->flags & flgConst) {
            logError(
                ctx->L,
                &node->loc,
                "prefix operation '{s}' cannot be performed on a constant",
                (FormatArg[]){{.s = getUnaryOpString(node->unaryExpr.op)}});
            operand = ERROR_TYPE(ctx);
            break;
        }
        // fallthrough
    case opMinus:
    case opPlus:
        if (!isNumericType(operand)) {
            logError(ctx->L,
                     &node->unaryExpr.operand->loc,
                     "postfix expression '{s}' no supported on type '{t}'",
                     (FormatArg[]){{.s = getUnaryOpString(node->unaryExpr.op)},
                                   {.t = operand}});
            operand = ERROR_TYPE(ctx);
        }
        break;
    case opCompl:
        if (!isIntegerType(operand)) {
            logError(ctx->L,
                     &node->unaryExpr.operand->loc,
                     "prefix expression '{s}' no supported on type '{t}'",
                     (FormatArg[]){{.s = getUnaryOpString(node->unaryExpr.op)},
                                   {.t = operand}});
            operand = ERROR_TYPE(ctx);
        }
        break;
    case opSpread:
        if (!typeIs(operand, Tuple)) {
            logError(
                ctx->L,
                &node->unaryExpr.operand->loc,
                "prefix spread expression '{s}' no supported on type '{t}'",
                (FormatArg[]){{.s = getUnaryOpString(node->unaryExpr.op)},
                              {.t = operand}});
            operand = ERROR_TYPE(ctx);
            break;
        }
        spreadTupleExpr(visitor, operand, node);
        operand = node->type;
        break;
    case opNot:
        if (operand != getPrimitiveType(ctx->typeTable, prtBool)) {
            logError(ctx->L,
                     &node->unaryExpr.operand->loc,
                     "logical '!' operator no supported on type '{t}', "
                     "expecting bool type",
                     (FormatArg[]){{.t = operand}});
            operand = ERROR_TYPE(ctx);
        }
        break;
    case opDeref:
        if (!typeIs(operand, Pointer)) {
            if (!transformToDerefOperator(visitor, node)) {
                logError(ctx->L,
                         &node->unaryExpr.operand->loc,
                         "cannot not dereference an non-pointer type '{t}'",
                         (FormatArg[]){{.t = operand}});
                operand = ERROR_TYPE(ctx);
            }
            else {
                operand = node->type;
            }
        }
        else {
            node->flags |=
                (operand->flags | node->unaryExpr.operand->flags & flgConst);
            operand = operand->pointer.pointed;
        }
        break;
    case opDelete:
        if (typeIs(operand, Primitive)) {
            logError(
                ctx->L, &node->loc, "cannot delete a primitive value", NULL);
            operand = ERROR_TYPE(ctx);
        }
        else {
            operand = makeVoidType(ctx->typeTable);
        }
        break;
    default:
        operand = ERROR_TYPE(ctx);
        break;
    }

    return operand;
}

void generateDeleteExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "cxy_free((void *)", NULL);
    astConstVisit(visitor, node->unaryExpr.operand);
    format(ctx->state, ")", NULL);
}

void generateAddressOfExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "&", NULL);
    astConstVisit(visitor, node->unaryExpr.operand);
}

void generateUnaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (node->unaryExpr.isPrefix) {
        switch (node->unaryExpr.op) {
        case opDelete:
            generateDeleteExpr(visitor, node);
            break;
        case opDeref:
            format(ctx->state, "(*", NULL);
            astConstVisit(visitor, node->unaryExpr.operand);
            format(ctx->state, ")", NULL);
            break;

        default:
            format(ctx->state,
                   "{s}",
                   (FormatArg[]){{.s = getUnaryOpString(node->unaryExpr.op)}});
            astConstVisit(visitor, node->unaryExpr.operand);
        }
    }
    else {
        astConstVisit(visitor, node->unaryExpr.operand);
        format(ctx->state,
               "{s}",
               (FormatArg[]){{.s = getUnaryOpString(node->unaryExpr.op)}});
    }
}

void checkAddressOfExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *operand = evalType(visitor, node->unaryExpr.operand);
    node->flags |= node->unaryExpr.operand->flags;
    node->type = makePointerType(
        ctx->typeTable, operand, node->unaryExpr.operand->flags);
}

void checkUnaryExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *operand = evalType(visitor, node->unaryExpr.operand);
    node->flags |= node->unaryExpr.operand->flags;

    if (node->unaryExpr.isPrefix) {
        node->type = checkPrefixExpr(visitor, operand, node);
        return;
    }

    node->type = operand;
    if (node->flags & flgConst) {
        logError(ctx->L,
                 &node->loc,
                 "postfix operation '{s}' cannot be performed on a constant",
                 (FormatArg[]){{.s = getUnaryOpString(node->unaryExpr.op)}});
        operand = ERROR_TYPE(ctx);
    }
    else if (!isNumericType(operand)) {
        logError(ctx->L,
                 &node->unaryExpr.operand->loc,
                 "postfix expression '{s}' not supported on type '{t}'",
                 (FormatArg[]){{.s = getUnaryOpString(node->unaryExpr.op)},
                               {.t = operand}});
        operand = ERROR_TYPE(ctx);
    }
}
