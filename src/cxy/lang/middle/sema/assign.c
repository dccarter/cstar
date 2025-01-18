//
// Created by Carter Mbotho on 2024-01-09.
//

#include "check.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"

static void checkIndexExprAssignment(AstVisitor *visitor, AstNode *node)
{
    // change X.[a] = y to X.op_idx_assign(a, y);
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *left = node->assignExpr.lhs;
    const Type *lhs = left->indexExpr.target->type;
    const Type *target = stripPointer(lhs);

    const Type *func = findMemberInType(lhs, S_IndexAssignOverload);
    if (func == NULL) {
        logError(ctx->L,
                 &node->assignExpr.rhs->loc,
                 "cannot assign to struct index expression, type '{t}' does "
                 "not overload index assignment `[]=` operator",
                 (FormatArg[]){{.t = target}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    AstNode *args = left->indexExpr.index;
    args->next = node->assignExpr.rhs;

    transformToMemberCallExpr(
        visitor, node, left->indexExpr.target, S_IndexAssignOverload, args);
    checkType(visitor, node);
}

void checkAssignExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *left = node->assignExpr.lhs, *right = node->assignExpr.rhs;

    const Type *lhs = NULL;
    u64 flags = flgNone;
    if (nodeIs(left, IndexExpr)) {
        const Type *target = checkType(visitor, left->indexExpr.target);
        if (typeIs(target, Error)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }

        if (isClassOrStructType(target)) {
            checkIndexExprAssignment(visitor, node);
            return;
        }
        target = unwrapType(target, &flags);
        if (isPointerType(target)) {
            lhs = typeIs(target, Pointer) ? target->pointer.pointed
                                          : target->array.elementType;
            left->type = lhs;
            const Type *index = checkType(visitor, left->indexExpr.index);
            if (typeIs(index, Error)) {
                node->type = ERROR_TYPE(ctx);
                return;
            }

            if (!isIntegerType(index)) {
                logError(ctx->L,
                         &left->indexExpr.index->loc,
                         "unexpected pointer index assignment operatior `[]=` "
                         "type, expecting an integer, got {t}",
                         (FormatArg[]){{.t = index}});
                node->type = ERROR_TYPE(ctx);
                return;
            }
        }
        else {
            logError(ctx->L,
                     &left->loc,
                     "index assignment not supported on type `{t}`",
                     (FormatArg[]){{.t = target}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
    }
    else {
        lhs = checkType(visitor, left);
    }

    if (typeIs(lhs, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }
    //    if (hasFlag(left, AddThis)) {
    //        AstNode *func = findEnclosingFunc(ctx->env, NULL, NULL);
    //        left->flags |= (func->flags & flgConst);
    //    }
    right->parentScope = node;
    const Type *rhs = checkType(visitor, right);
    if (typeIs(rhs, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    bool isLeftAuto = typeIs(lhs, Auto);

    // TODO check r-value-ness
    if ((flags & flgConst) || hasFlag(left, Const) || hasFlag(lhs, Const)) {
        logError(ctx->L,
                 &node->loc,
                 "lhs of assignment expressions is a constant",
                 (FormatArg[]){{.t = lhs}});
        node->type = ERROR_TYPE(ctx);
    }
    else if (typeIs(rhs, Array) && rhs->array.len != UINT64_MAX) {
        if (isLeftAuto)
            logError(ctx->L,
                     &node->loc,
                     "array assignment not allowed, assignment should be done "
                     "at initialisation",
                     NULL);
        else
            logError(
                ctx->L, &node->loc, "assign to an array is not allowed", NULL);
        node->type = ERROR_TYPE(ctx);
    }
    else if (!isTypeAssignableFrom(lhs, rhs) &&
             !(typeIs(lhs, Pointer) && typeIs(rhs, Null))) {
        logError(ctx->L,
                 &node->assignExpr.rhs->loc,
                 "incompatible types on assigment expression, expecting '{t}', "
                 "got '{t}'",
                 (FormatArg[]){{.t = lhs}, {.t = rhs}});
        node->type = ERROR_TYPE(ctx);
    }

    if (node->type == ERROR_TYPE(ctx))
        return;

    if (isLeftAuto) {
        csAssert0(nodeIs(left, Path));
        AstNode *resolvesTo = left->path.elements->pathElement.resolvesTo;
        csAssert0(nodeIs(resolvesTo, VarDecl));
        resolvesTo->type = rhs;
        left->path.elements->type = rhs;
        left->type = rhs;
        node->type = rhs;
    }
    else {
        node->type = lhs;
    }

    node->assignExpr.rhs = transformToUnionValue(ctx, right, lhs, rhs);

    if (!hasFlag(lhs, Optional) || hasFlag(rhs, Optional))
        return;

    const Type *target = getOptionalTargetType(lhs);
    if (nodeIs(right, NullLit)) {
        if (!transformOptionalNone(visitor, right, target))
            node->type = ERROR_TYPE(ctx);
    }
    else {
        right->type = target;
        if (!transformOptionalSome(
                visitor, right, copyAstNode(ctx->pool, right)))
            node->type = ERROR_TYPE(ctx);
    }
}
