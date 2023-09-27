//
// Created by Carter on 2023-08-30.
//

#include "../check.h"
#include "../codegen.h"
#include "../eval.h"

#include "lang/flag.h"
#include "lang/ttable.h"

#include "core/alloc.h"

static void generateTupleDelete(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    format(state, "attr(always_inline)\nstatic void ", NULL);
    writeTypename(context, type);
    format(state, "__op__destructor(void *ptr) {{{>}\n", NULL);
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
                   "CXY__free((void *)this->_{u64});",
                   (FormatArg[]){{.u64 = i}});
        }
        else if (typeIs(stripped, Struct) || typeIs(stripped, Array) ||
                 typeIs(stripped, Tuple)) {
            writeTypename(context, stripped);
            format(state,
                   "__op__destructor(&this->_{u64});",
                   (FormatArg[]){{.u64 = i}});
        }
    }

    format(state, "{<}\n}", NULL);
}

void generateTupleDefinition(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;

    format(state, "typedef struct {{{>}\n", NULL);
    format(state, "void *__mm;\n", NULL);
    for (u64 i = 0; i < type->tuple.count; i++) {
        if (i != 0)
            format(state, "\n", NULL);
        generateTypeUsage(context, type->tuple.members[i]);
        format(state, " _{u64};", (FormatArg[]){{.u64 = i}});
    }
    format(state, "{<}\n} ", NULL);
    writeTypename(context, type);

    format(state, ";\n", NULL);
}

void generateTupleExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *arg = node->tupleExpr.elements;

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
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *elements = node->tupleExpr.elements, *element = elements;
    node->tupleExpr.len = node->tupleExpr.len ?: countAstNodes(elements);
    const Type **elements_ = mallocOrDie(sizeof(Type *) * node->tupleExpr.len);

    for (u64 i = 0; element; element = element->next, i++) {
        elements_[i] = checkType(visitor, element);
        if (typeIs(elements_[i], Error))
            node->type = element->type;
    }

    if (!typeIs(node->type, Error)) {
        node->type = makeTupleType(
            ctx->types, elements_, node->tupleExpr.len, node->flags & flgConst);
    }

    free(elements_);
}

void checkTupleType(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *elems = node->tupleType.elements, *elem = elems;
    u64 count = node->tupleType.len ?: countAstNodes(node->tupleType.elements);
    const Type **elems_ = mallocOrDie(sizeof(Type *) * count);

    for (u64 i = 0; elem; elem = elem->next, i++) {
        elems_[i] = checkType(visitor, elem);
        if (typeIs(elems_[i], Error))
            node->type = ERROR_TYPE(ctx);
    }

    if (typeIs(node->type, Error)) {
        free(elems_);
        return;
    }

    node->type =
        makeTupleType(ctx->types, elems_, count, node->flags & flgConst);

    free(elems_);
}

void evalTupleExpr(AstVisitor *visitor, AstNode *node)
{
    u64 i = 0;
    AstNode *elem = node->tupleExpr.elements;
    for (; elem; elem = elem->next, i++) {
        if (!evaluate(visitor, elem)) {
            node->tag = astError;
            return;
        }
    }
    node->tupleExpr.len = i;
}
