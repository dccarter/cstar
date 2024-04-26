//
// Created by Carter Mbotho on 2024-01-09.
//

#include "check.h"

#include "lang/frontend/flag.h"

static void spreadTupleExpr(AstVisitor *visitor,
                            const Type *operand,
                            AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->unaryExpr.operand;
    AstNode *parts = NULL, *part = NULL;

    for (i64 i = 0; i < operand->tuple.count; i++) {
        const Type *type = operand->tuple.members[i];
        AstNode *next = makeAstNode(
            ctx->pool,
            &node->loc,
            &(AstNode){
                .tag = astMemberExpr,
                .flags = expr->flags,
                .memberExpr = {
                    .target = shallowCloneAstNode(ctx->pool, expr),
                    .member = makeAstNode(ctx->pool,
                                          &node->loc,
                                          &(AstNode){.flags = type->flags,
                                                     .type = type,
                                                     .tag = astIntegerLit,
                                                     .intLiteral.value = i})}});
        type = checkType(visitor, next);
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
    TypingContext *ctx = getAstVisitorContext(visitor);
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
    case opAwait:
        if (!isClassOrStructType(operand)) {
            logError(ctx->L,
                     &node->unaryExpr.operand->loc,
                     "type '{t}' does not implement the await operator",
                     (FormatArg[]){{.t = operand}});
            operand = ERROR_TYPE(ctx);
            break;
        }
        else if (transformToAwaitOperator(visitor, node)) {
            operand = node->type;
        }
        else {
            logError(ctx->L,
                     &node->unaryExpr.operand->loc,
                     "struct '{t}' does not `await` operator",
                     (FormatArg[]){{.t = operand}});
            operand = ERROR_TYPE(ctx);
        }
        break;
    case opNot:
        if (isClassOrStructType(operand)) {
            if (transformToTruthyOperator(visitor, node->unaryExpr.operand)) {
                operand = node->unaryExpr.operand->type;
            }
            else {
                logError(ctx->L,
                         &node->unaryExpr.operand->loc,
                         "struct '{t}' does not overload dereference "
                         "`deref` operator",
                         (FormatArg[]){{.t = operand}});
                operand = ERROR_TYPE(ctx);
            }
        }
        else if (operand != getPrimitiveType(ctx->types, prtBool)) {
            logError(ctx->L,
                     &node->unaryExpr.operand->loc,
                     "logical '!' operator no supported on type '{t}', "
                     "expecting bool type",
                     (FormatArg[]){{.t = operand}});
            operand = ERROR_TYPE(ctx);
        }
        break;
    case opDeref:
        if (isClassOrStructType(operand)) {
            FileLoc loc = node->unaryExpr.operand->loc;
            if (transformToDerefOperator(visitor, node)) {
                operand = node->type;
            }
            else {
                logError(ctx->L,
                         &loc,
                         "struct '{t}' does not overload dereference "
                         "`deref` operator",
                         (FormatArg[]){{.t = operand}});
                operand = ERROR_TYPE(ctx);
            }
        }
        else if (isPointerType(operand)) {
            node->tag = astIndexExpr;
            node->type = operand->pointer.pointed;
            node->indexExpr.target = node->unaryExpr.operand;
            node->indexExpr.index =
                makeUnsignedIntegerLiteral(ctx->pool,
                                           &node->loc,
                                           0,
                                           NULL,
                                           getPrimitiveType(ctx->types, prtI8));
            operand = operand->pointer.pointed;
        }
        else {
            logError(ctx->L,
                     &node->unaryExpr.operand->loc,
                     "cannot not dereference an non-pointer type '{t}'",
                     (FormatArg[]){{.t = operand}});
            operand = ERROR_TYPE(ctx);
        }
        break;
    case opMove:
        if (!nodeIsLeftValue(node->unaryExpr.operand) ||
            hasFlag(node->unaryExpr.operand, Move)) {
            logError(ctx->L,
                     &node->unaryExpr.operand->loc,
                     "operand of a move operator must be a left value",
                     NULL);
            operand = ERROR_TYPE(ctx);
        }
        else {
            operand = node->unaryExpr.operand->type;
        }
        break;
    default:
        logError(ctx->L,
                 &node->unaryExpr.operand->loc,
                 "unsupported unary operator '{s}' on type '{t}'",
                 (FormatArg[]){{.s = getUnaryOpString(node->unaryExpr.op)},
                               {.t = operand}});
        operand = ERROR_TYPE(ctx);
        break;
    }

    return operand;
}

void checkAddressOfExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *operand = checkType(visitor, node->unaryExpr.operand);
    if (typeIs(operand, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    node->flags |= node->unaryExpr.operand->flags;
    node->type = makePointerType(ctx->types, operand, node->flags);
}

void checkUnaryExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *operand = checkType(visitor, node->unaryExpr.operand);
    if (typeIs(operand, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

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
