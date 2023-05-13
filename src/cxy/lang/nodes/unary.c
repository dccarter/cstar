//
// Created by Carter on 2023-04-18.
//

#include "lang/codegen.h"
#include "lang/semantics.h"

#include "lang/ttable.h"

#include "core/alloc.h"

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
        if (!typeIs(operand, Pointer) && !typeIs(operand, Array)) {
            logError(ctx->L,
                     &node->loc,
                     "cannot delete an none `new` allocated object",
                     NULL);
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
    const Type *type = node->unaryExpr.operand->type, *raw = stripPointer(type);
    if (isBuiltinType(raw)) {
        format(ctx->state, "cxy_free((void *)", NULL);
    }
    else {
        writeTypename(ctx, raw);
        format(ctx->state, "__op_delete(", NULL);
    }
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
