//
// Created by Carter on 2023-04-18.
//

#include "lang/codegen.h"
#include "lang/semantics.h"

#include "lang/capture.h"
#include "lang/ttable.h"

#include "core/alloc.h"

#include <memory.h>

static inline bool isParentAssignExpr(const AstNode *node)
{
    return node->parentScope && nodeIs(node->parentScope, AssignExpr);
}

static void checkIndexOperator(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *target = stripPointer(node->indexExpr.target->type);
    csAssert0(!isParentAssignExpr(node));

    AstNode *func = findSymbolOnly(target->tStruct.env, "op_idx");
    if (func == NULL) {
        logError(ctx->L,
                 &node->indexExpr.target->loc,
                 "index expression target type '{t}' does not overload the "
                 "index operator `[]`",
                 (FormatArg[]){{.t = target}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    const Type *index = evalType(visitor, node->indexExpr.index);
    if (typeIs(index, Error)) {
        node->type = index;
        return;
    }

    transformToMemberCallExpr(visitor,
                              node,
                              func,
                              node->indexExpr.target,
                              "op_idx",
                              node->indexExpr.index);
    evalType(visitor, node);
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
        if (typeIs(target, Pointer))
            format(ctx->state, "->data", NULL);
        else
            format(ctx->state, ".data", NULL);
    }

    if (typeIs(target, Pointer) && typeIs(stripped, Array))
        format(ctx->state, ")", NULL);

    format(ctx->state, "[", NULL);
    astConstVisit(visitor, node->indexExpr.index);
    format(ctx->state, "]", NULL);
}

void checkIndex(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *target = evalType(visitor, node->indexExpr.target);
    node->flags |= node->indexExpr.target->flags;

    if (typeIs(target, Pointer)) {
        target = stripPointer(target);
        node->indexExpr.target = makeAstNode(
            ctx->pool,
            &node->indexExpr.target->loc,
            &(AstNode){.tag = astUnaryExpr,
                       .type = target,
                       .flags = node->indexExpr.target->flags,
                       .unaryExpr = {.op = opDeref,
                                     .operand = node->indexExpr.target,
                                     .isPrefix = true}});
    }

    if (target->tag == typArray) {
        astVisit(visitor, node->indexExpr.index);
        node->type = target->array.elementType;
    }
    else if (target->tag == typMap) {
        astVisit(visitor, node->indexExpr.index);
        node->type = target->map.value;
    }
    else if (target->tag == typStruct || target->tag == typUnion) {
        checkIndexOperator(visitor, node);
    }
    else {
        logError(ctx->L,
                 &node->loc,
                 "index operator (.[]) not supported on type '{t}'",
                 (FormatArg[]){{.t = target}});
    }
}