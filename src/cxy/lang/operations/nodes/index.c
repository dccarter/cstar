//
// Created by Carter on 2023-04-18.
//

#include "../check.h"
#include "../codegen.h"
#include "../eval.h"

#include "lang/flag.h"
#include "lang/operations.h"
#include "lang/strings.h"
#include "lang/ttable.h"

#include "core/alloc.h"

#include <memory.h>

static inline bool isParentAssignExpr(const AstNode *node)
{
    return nodeIs(node->parentScope, AssignExpr);
}

static void checkIndexOperator(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *target = stripAll(node->indexExpr.target->type);
    csAssert0(!isParentAssignExpr(node));

    const Type *func = findStructMemberType(target, S_IndexOverload);
    if (func == NULL) {
        logError(ctx->L,
                 &node->indexExpr.target->loc,
                 "index expression target type '{t}' does not overload the "
                 "index operator `[]`",
                 (FormatArg[]){{.t = target}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    const Type *index = checkType(visitor, node->indexExpr.index);
    if (typeIs(index, Error)) {
        node->type = index;
        return;
    }

    transformToMemberCallExpr(visitor,
                              node,
                              node->indexExpr.target,
                              S_IndexOverload,
                              node->indexExpr.index);
    checkType(visitor, node);
}

static bool evalStringIndexExpr(EvalContext *ctx, AstNode *node)
{
    AstNode *target = node->indexExpr.target;
    AstNode *index = node->indexExpr.index;

    if (!nodeIs(target, EnumDecl) && !nodeIs(target, StructDecl)) {
        logError(ctx->L,
                 &target->loc,
                 "comp-time string index operator only supported on enum or "
                 "struct declaration typeinfo instances",
                 NULL);
        node->tag = astError;
        return false;
    }

    AstNode *member =
        nodeIs(target, EnumDecl)
            ? findEnumOptionByName(target, index->stringLiteral.value)
            : findMemberByName(target, index->stringLiteral.value);

    if (member == NULL)
        node->tag = astNullLit;
    else
        *node = *member;
    return true;
}

static bool evalIntegerIndexExpr(EvalContext *ctx, AstNode *node)
{
    AstNode *target = node->indexExpr.target;
    AstNode *index = node->indexExpr.index;

    if (!nodeIs(target, ArrayExpr) && !nodeIs(target, StringLit)) {
        logError(ctx->L,
                 &target->loc,
                 "comp-time integer index operator only supported on string or "
                 "array "
                 "expressions",
                 NULL);
        node->tag = astError;
        return false;
    }

    i64 i = (i64)getNumericLiteral(index);
    u64 len = nodeIs(target, StringLit) ? strlen(target->stringLiteral.value)
                                        : target->arrayExpr.len;
    if (i < 0 || i >= len) {
        logError(ctx->L,
                 &node->loc,
                 "index out of bounds for comp-time index "
                 "expression, requested index '{i64}', expecting '< {u64'}",
                 (FormatArg[]){{.i64 = i}, {.u64 = len}});

        node->tag = astError;
        return false;
    }

    if (nodeIs(target, StringLit)) {
        clearAstBody(node);
        node->tag = astCharLit;
        node->charLiteral.value = (wchar)target->stringLiteral.value[i];
    }
    else {
        *node = *getNodeAtIndex(target->arrayExpr.elements, i);
    }

    return true;
}

void generateIndexExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const Type *target = node->indexExpr.target->type;
    const Type *stripped = stripPointer(target);

    if (typeIs(target, Pointer) && typeIs(stripped, Array))
        format(ctx->state, "(*", NULL);

    astConstVisit(visitor, node->indexExpr.target);

    if (isSliceType(target)) {
        format(ctx->state, "->data", NULL);
    }

    if (typeIs(target, Pointer) && typeIs(stripped, Array))
        format(ctx->state, ")", NULL);

    format(ctx->state, "[", NULL);
    astConstVisit(visitor, node->indexExpr.index);
    format(ctx->state, "]", NULL);
}

void checkIndexExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *target = checkType(visitor, node->indexExpr.target);
    if (typeIs(target, Error)) {
        node->type = target;
        return;
    }

    const Type *index = checkType(visitor, node->indexExpr.index);
    if (typeIs(index, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    node->flags |= node->indexExpr.target->flags;
    const Type *unwrapped = unwrapType(target, NULL),
               *stripped = stripAll(target);
    if (typeIs(unwrapped, Array)) {
        if (!isIntegerType(index)) {
            logError(ctx->L,
                     &node->indexExpr.index->loc,
                     "unexpected array expression index type, expecting an "
                     "integer, got '{t}'",
                     (FormatArg[]){{.t = index}});
            node->type = ERROR_TYPE(ctx);
        }
        else
            node->type = unwrapped->array.elementType;
    }
    else if (typeIs(unwrapped, Map)) {
        if (!isTypeAssignableFrom(unwrapped->map.key, index)) {
            logError(ctx->L,
                     &node->indexExpr.index->loc,
                     "unexpected map expression index type, expecting type "
                     "'{t}', got '{t}'",
                     (FormatArg[]){{.t = unwrapped->map.key}, {.t = index}});
            node->type = ERROR_TYPE(ctx);
        }
        else
            node->type = unwrapped->map.value;
    }
    else if (typeIs(unwrapped, Struct) || typeIs(unwrapped, Class)) {
        checkIndexOperator(visitor, node);
    }
    else if (typeIs(unwrapped, Pointer)) {
        if (!isIntegerType(index)) {
            logError(
                ctx->L,
                &node->indexExpr.index->loc,
                "unexpected pointer offset expression index type, expecting an "
                "integer, got '{t}'",
                (FormatArg[]){{.t = index}});
            node->type = ERROR_TYPE(ctx);
        }
        else
            node->type = unwrapped->pointer.pointed;
    }
    else if (typeIs(unwrapped, String)) {
        if (!isIntegerType(index)) {
            logError(ctx->L,
                     &node->loc,
                     "unexpected index type on operator (.[]), expecting "
                     "integer, got '{t}'",
                     (FormatArg[]){{.t = index}});

            node->type = ERROR_TYPE(ctx);
        }
        else
            node->type = getPrimitiveType(ctx->types, prtChar);
    }
    else {
        logError(ctx->L,
                 &node->loc,
                 "index operator (.[]) not supported on type '{t}'",
                 (FormatArg[]){{.t = target}});

        node->type = ERROR_TYPE(ctx);
    }
}

void evalIndexExpr(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    AstNode *target = node->indexExpr.target;

    if (!evaluate(visitor, target)) {
        node->tag = astError;
        return;
    }

    AstNode *index = node->indexExpr.index;
    if (!evaluate(visitor, index)) {
        node->tag = astError;
        return;
    }

    if (nodeIs(index, StringLit)) {
        evalStringIndexExpr(ctx, node);
    }
    else if (nodeIs(index, IntegerLit)) {
        evalIntegerIndexExpr(ctx, node);
    }
    else {
        logError(ctx->L,
                 &node->loc,
                 "unexpected comp-time index expression, index can either be a "
                 "string or integer literal",
                 NULL);
        node->tag = astError;
    }
}
