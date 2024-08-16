//
// Created by Carter Mbotho on 2024-01-09.
//

#include "check.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"

static inline bool isParentAssignExpr(const AstNode *node)
{
    return nodeIs(node->parentScope, AssignExpr);
}

static void checkIndexOperator(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *target = stripAll(node->indexExpr.target->type);
    // csAssert0(!isParentAssignExpr(node));

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
    const Type *unwrapped = stripReference(unwrapType(target, NULL)),
               *stripped = stripAll(target);
    if (typeIs(stripped, Array)) {
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
            node->type = getPrimitiveType(ctx->types, prtCChar);
    }
    else {
        logError(ctx->L,
                 &node->loc,
                 "index operator (.[]) not supported on type '{t}'",
                 (FormatArg[]){{.t = target}});

        node->type = ERROR_TYPE(ctx);
    }
}
