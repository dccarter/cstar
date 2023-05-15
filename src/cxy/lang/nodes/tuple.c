//
// Created by Carter on 2023-04-18.
//

#include "lang/codegen.h"
#include "lang/eval.h"
#include "lang/semantics.h"

#include "lang/ttable.h"

#include "core/alloc.h"

static void generateTupleDelete(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    format(state, "attr(always_inline)\nstatic void ", NULL);
    writeTypename(context, type);
    format(state, "__builtin_destructor(void *ptr) {{{>}\n", NULL);
    writeTypename(context, type);
    format(state, " *this = ptr;\n", NULL);

    u64 y = 0;
    for (u64 i = 0; i < type->tuple.count; i++) {
        const Type *member = type->tuple.members[i];
        const Type *stripped = stripAll(member);
        const Type *unwrapped = unwrapType(member, NULL);

        if (y++ != 0)
            format(state, "\n", NULL);

        if (typeIs(unwrapped, Pointer) || typeIs(unwrapped, String)) {
            format(state,
                   "cxy_free((void *)this->_{u64});",
                   (FormatArg[]){{.u64 = i}});
        }
        else if (typeIs(stripped, Struct) || typeIs(stripped, Array) ||
                 typeIs(stripped, Tuple)) {
            writeTypename(context, stripped);
            format(state,
                   "__builtin_destructor(&this->_{u64});",
                   (FormatArg[]){{.u64 = i}});
        }
    }

    format(state, "{<}\n}", NULL);
}

void generateTupleDefinition(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;

    format(state, "typedef struct {{{>}\n", NULL);
    for (u64 i = 0; i < type->tuple.count; i++) {
        if (i != 0)
            format(state, "\n", NULL);
        generateTypeUsage(context, type->tuple.members[i]);
        format(state, " _{u64};", (FormatArg[]){{.u64 = i}});
    }
    format(state, "{<}\n} ", NULL);
    writeTypename(context, type);

    format(state, ";\n", NULL);
    generateTupleDelete(context, type);
}

void generateTupleExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *arg = node->tupleExpr.args;

    format(ctx->state, "(", NULL);
    generateTypeUsage(ctx, node->type);
    format(ctx->state, ")", NULL);

    format(ctx->state, "{{", NULL);
    for (u64 i = 0; arg; arg = arg->next, i++) {
        if (i != 0)
            format(ctx->state, ", ", NULL);
        format(ctx->state, "._{u64} = ", (FormatArg[]){{.u64 = i}});
        astConstVisit(visitor, arg);
    }

    format(ctx->state, "}", NULL);
}

void checkTupleExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    u64 count = countAstNodes(node->tupleExpr.args);
    const Type **args = mallocOrDie(sizeof(Type *) * count);
    AstNode *arg = node->tupleExpr.args;

    for (u64 i = 0; arg; arg = arg->next, i++) {
        args[i] = evalType(visitor, arg);
        if (args[i] == ERROR_TYPE(ctx))
            node->type = ERROR_TYPE(ctx);
    }

    if (node->type == NULL) {
        node->type = makeTupleType(ctx->typeTable, args, count, flgNone);
    }

    free(args);
}

void checkTupleType(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    u64 count = countAstNodes(node->tupleType.args);
    const Type **args = mallocOrDie(sizeof(Type *) * count);

    AstNode *arg = node->tupleType.args;
    for (u64 i = 0; arg; arg = arg->next, i++) {
        args[i] = evalType(visitor, arg);
        if (args[i] == ERROR_TYPE(ctx))
            node->type = ERROR_TYPE(ctx);
    }

    if (node->type == NULL)
        node->type = makeTupleType(ctx->typeTable, args, count, flgNone);

    free(args);
}

void evalTupleExpr(AstVisitor *visitor, AstNode *node)
{
    u64 i = 0;
    AstNode *arg = node->tupleExpr.args;
    for (; arg; arg = arg->next, i++) {
        if (!evaluate(visitor, arg)) {
            node->tag = astError;
            return;
        }
    }
    node->tupleExpr.len = i;
}
