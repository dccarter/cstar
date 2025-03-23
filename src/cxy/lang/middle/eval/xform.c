//
// Created by Carter Mbotho on 2025-03-12.
//

#include "eval.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/ttable.h"

#include <core/alloc.h>

void evalXformType(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!evaluate(visitor, node->xForm.target)) {
        node->tag = astError;
        return;
    }
    const Type *target = resolveUnThisUnwrapType(node->xForm.target->type);
    if (!typeIs(target, Tuple)) {
        logError(ctx->L,
                 &node->xForm.target->loc,
                 "unsupported type {t}, transforms only supported on tuple "
                 "extressions",
                 (FormatArg[]){{.t = target ?: makeErrorType(ctx->types)}});
        node->tag = astError;
        return;
    }

    const Type **members =
        mallocOrDie(sizeof(const Type *) * target->tuple.count);
    AstNode *arg1 = node->xForm.args, *arg2 = arg1->next;
    int j = 0;
    for (int i = 0; i < target->tuple.count; i++) {
        const Type *member = target->tuple.members[i];
        arg1->varDecl.init =
            makeTypeReferenceNode(ctx->pool, member, &arg1->loc);
        arg1->type = member;
        if (arg2) {
            arg2->varDecl.init =
                makeIntegerLiteral(ctx->pool,
                                   &arg2->loc,
                                   i,
                                   NULL,
                                   getPrimitiveType(ctx->types, prtI64));
            arg2->type = arg2->varDecl.init->type;
        }
        AstNode *cond = deepCloneAstNode(ctx->pool, node->xForm.cond);
        if (cond) {
            if (!evaluate(visitor, cond)) {
                node->tag = astError;
                free(members);
                return;
            }
            if (!nodeIs(cond, BoolLit)) {
                logError(ctx->L,
                         &node->xForm.cond->loc,
                         "comptime transform condition must evaluate to a "
                         "boolean literal",
                         NULL);
                node->tag = astError;
                free(members);
                return;
            }
            if (!cond->boolLiteral.value)
                continue;
        }

        AstNode *xForm = deepCloneAstNode(ctx->pool, node->xForm.xForm);
        const Type *result = evalType(ctx, xForm);
        if (typeIs(result, Error)) {
            node->tag = astError;
            free(members);
            return;
        }
        members[j++] = xForm->type;
    }

    const Type *type = makeTupleType(ctx->types, members, j, flgNone);
    free(members);
    node->tag = astTypeRef;
    node->type = type;
    clearAstBody(node);
}