//
// Created by Carter Mbotho on 2023-11-01.
//

#include "../check.h"
#include "../codegen.h"
#include "../eval.h"

#include "lang/flag.h"
#include "lang/ttable.h"

#include "core/alloc.h"

AstNode *transformToUnionValue(TypingContext *ctx,
                               AstNode *right,
                               const Type *lhs,
                               const Type *rhs)
{
    const Type *stripped = stripOnce(lhs, NULL);
    if (rhs != lhs && typeIs(stripped, Union) && stripped != rhs) {
        u32 idx = findUnionTypeIndex(stripped, rhs);
        csAssert0(idx != UINT32_MAX);
        return makeUnionValueExpr(
            ctx->pool, &right->loc, right->flags, right, idx, NULL, lhs);
    }
    return right;
}

void generateUnionDefinition(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;

    format(state, "typedef struct _", NULL);
    writeTypename(context, type);
    format(state, " {{{>}\n", NULL);
    format(state, "u32 tag;\n", NULL);
    format(state, "union {{{>}\n", NULL);
    for (u64 i = 0; i < type->tUnion.count; i++) {
        if (i != 0)
            format(state, "\n", NULL);
        generateTypeUsage(context, type->tUnion.members[i]);
        format(state, " _{u64};", (FormatArg[]){{.u64 = i}});
    }
    format(state, "{<}\n};", NULL);
    format(state, "{<}\n} ", NULL);
    writeTypename(context, type);

    format(state, ";\n", NULL);
}

void generateUnionValueExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    FormatState *state = ctx->state;
    format(state, "(", NULL);
    generateTypeUsage(ctx, node->type);
    format(state,
           "){{.tag = {u32}, ._{u32} = ",
           (FormatArg[]){{.u32 = node->unionValue.idx},
                         {.u32 = node->unionValue.idx}});
    astConstVisit(visitor, node->unionValue.value);
    format(state, "}", NULL);
}