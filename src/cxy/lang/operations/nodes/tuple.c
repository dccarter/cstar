//
// Created by Carter on 2023-08-30.
//

#include "../check.h"
#include "../codegen.h"
#include "../eval.h"

#include "lang/flag.h"
#include "lang/ttable.h"

#include "core/alloc.h"

static void generateTupleCopy(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    format(state, "\nattr(always_inline)\nstatic ", NULL);
    generateTypeUsage(context, type);
    format(state, " ", NULL);
    writeTypename(context, type);
    format(state, "__op__copy(", NULL);
    generateTypeUsage(context, type);
    format(state, " *this) {{{>}\n", NULL);

    u64 y = 0;
    format(state, "return (", NULL);
    generateTypeUsage(context, type);
    format(state, "){{", NULL);

    for (u64 i = 0; i < type->tuple.count; i++) {
        const Type *member = type->tuple.members[i];
        if (y++ != 0)
            format(state, ", ", NULL);

        format(state, "._{u64} = ", (FormatArg[]){{.u64 = i}});
        if (!hasReferenceMembers(member)) {
            format(state, "this->_{u64}", (FormatArg[]){{.u64 = i}});
            continue;
        }

        if (isClassType(member)) {
            format(state, "(", NULL);
            generateTypeUsage(context, member);
            format(state,
                   ")CXY__default_get_ref(this->_{u64})",
                   (FormatArg[]){{.u64 = i}});
        }
        else if (isStructType(member) || isTupleType(member)) //
        {
            generateTypeUsage(context, member);
            format(context->state,
                   "__op__copy(&(this->_{u64}))",
                   (FormatArg[]){{.u64 = i}});
        }
    }

    format(state, "};{<}\n}\n", NULL);
}

static void generateTupleDestructor(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    format(state, "\nstatic void ", NULL);
    writeTypename(context, type);
    format(state, "__op__destructor_fwd(void *ptr) {{{>}\n", NULL);
    writeTypename(context, type);
    format(state, " *this = ptr;\n", NULL);

    u64 y = 0;
    for (u64 i = 0; i < type->tuple.count; i++) {
        const Type *member = type->tuple.members[i];

        if (y != 0)
            format(state, "\n", NULL);

        if (isClassType(member) &&
            hasFlag(member->tClass.decl, ReferenceMembers)) //
        {
            format(context->state,
                   "CXY__free(this->_{u64});",
                   (FormatArg[]){{.u64 = i}});
            y++;
        }
        else if (isStructType(member) &&
                 hasFlag(member->tClass.decl, ReferenceMembers)) //
        {
            generateTypeUsage(context, member);
            format(context->state,
                   "__op__destructor_fwd(&(this->_{u64}));",
                   (FormatArg[]){{.u64 = i}});
            y++;
        }
    }

    format(state, "{<}\n}\n", NULL);
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

    if (hasFlag(type, ReferenceMembers)) {
        generateTupleCopy(context, type);
        generateTupleDestructor(context, type);
    }
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

    AstNode *prev = NULL;
    for (u64 i = 0; element; element = element->next, i++) {
        if (nodeIs(element, Nop)) {
            if (prev == NULL)
                node->tupleExpr.elements = element->next;
            else
                prev->next = element->next;
            continue;
        }
        prev = element;

        const Type *type = checkType(visitor, element);
        if (typeIs(type, Error)) {
            node->type = element->type;
            continue;
        }

        type = unwrapType(type, NULL);
        if (isClassOrStructType(type) && !hasFlag(type, Closure)) {
            AstNode *decl = getTypeDecl(type);
            node->flags |= (decl->flags & flgReferenceMembers);
        }
    }

    if (typeIs(node->type, Error))
        return;

    node->tupleExpr.len = countAstNodes(node->tupleExpr.elements);
    const Type **elements_ = mallocOrDie(sizeof(Type *) * node->tupleExpr.len);
    element = node->tupleExpr.elements;
    for (u64 i = 0; element; element = element->next, i++) {
        elements_[i] = element->type;
    }

    node->type = makeTupleType(
        ctx->types,
        elements_,
        node->tupleExpr.len,
        node->flags & (flgReferenceMembers | flgConst | flgTransient));

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
        const Type *type = unwrapType(elems_[i], NULL);
        if (typeIs(elems_[i], Error)) {
            node->type = ERROR_TYPE(ctx);
        }
        else if (isClassOrStructType(type) && !hasFlag(type, Closure)) {
            AstNode *decl = getTypeDecl(type);
            node->flags |= (decl->flags & flgReferenceMembers);
        }
    }

    if (typeIs(node->type, Error)) {
        free(elems_);
        return;
    }

    node->type = makeTupleType(
        ctx->types,
        elems_,
        count,
        node->flags & (flgReferenceMembers | flgConst | flgTransient));

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
