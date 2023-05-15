//
// Created by Carter on 2023-04-18.
//

#include "lang/codegen.h"
#include "lang/eval.h"
#include "lang/semantics.h"

#include "lang/ttable.h"

static void generateArrayDelete(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    const Type *element = type->array.elementType;
    const Type *raw = stripPointer(element);

    format(state, "attr(always_inline)\nstatic void ", NULL);
    writeTypename(context, type);
    format(state, "__op_delete(", NULL);
    if (isSliceType(type))
        writeTypename(context, type);
    else
        writeTypename(context, type->array.elementType);
    format(state, " *this) {{{>}\n", NULL);

    if (!typeIs(element, Func) && !typeIs(element, Generic) &&
        !(isBuiltinType(element) || typeIs(element, String))) {
        format(state, "for (u64 i = 0; i < ", NULL);
        if (isSliceType(type))
            format(state, "this->len", NULL);
        else
            format(state, "{u64}", (FormatArg[]){{.u64 = type->array.len}});
        format(state, "; i++) {{{>}\n", NULL);

        if ((typeIs(element, Pointer) && isBuiltinType(raw)) ||
            typeIs(element, String)) {
            if (isSliceType(type))
                format(state,
                       "if (this->data[i]) cxy_free((void *)this->data[i]);",
                       NULL);
            else
                format(state, "if (this[i]) cxy_free((void *)this[i]);", NULL);
        }
        else {
            if (isSliceType(type)) {
                if (typeIs(element, Pointer))
                    format(state,
                           "if (this->data[i]) cxy_free(this->data[i]);",
                           NULL);
                else {
                    writeTypename(context, raw);
                    format(state, "__op_delete(&this->data[i]);", NULL);
                }
            }
            else {
                if (typeIs(element, Pointer))
                    format(state, "if (this[i]) cxy_free(this[i]);", NULL);
                else {
                    writeTypename(context, raw);
                    format(state, "__op_delete(&this[i]);", NULL);
                }
            }
        }
        format(state, "{<}\n};\n", NULL);
    }

    format(state, "{<}\n}", NULL);

    generateDestructor(context, type);
}

void generateForStmtArray(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *var = node->forStmt.var;
    const AstNode *range = node->forStmt.range;

    cstring name = makeAnonymousVariable(ctx->strPool, "cyx_for");
    // create an array
    format(ctx->state, "{{{>}\n", NULL);
    if (range->tag == astArrayExpr)
        generateTypeUsage(ctx, range->type);
    else
        generateTypeUsage(
            ctx,
            &(const Type){.tag = typPointer,
                          .flags =
                              range->type->flags | node->forStmt.range->flags,
                          .pointer.pointed = range->type->array.elementType});

    format(ctx->state, " _arr_{s} = ", (FormatArg[]){{.s = name}});
    astConstVisit(visitor, range);
    if (isSliceType(range->type))
        format(ctx->state, ".data;\n", NULL);
    else
        format(ctx->state, ";\n", NULL);

    // create index variable
    format(ctx->state, "u64 _i_{s} = 0;\n", (FormatArg[]){{.s = name}});

    // Create actual loop variable
    generateTypeUsage(ctx, range->type->array.elementType);
    format(ctx->state, " ", NULL);
    astConstVisit(visitor, var->varDecl.names);
    format(ctx->state, " = _arr_{s}[0];\n", (FormatArg[]){{.s = name}});

    format(ctx->state, "for (; _i_{s} < ", (FormatArg[]){{.s = name}});

    if (isSliceType(range->type)) {
        astConstVisit(visitor, range);
        format(ctx->state, ".len", NULL);
    }
    else
        format(ctx->state,
               "{u64}",
               (FormatArg[]){{.u64 = range->type->array.len}});

    format(ctx->state, "; _i_{s}++, ", (FormatArg[]){{.s = name}});

    astConstVisit(visitor, var->varDecl.names);
    format(ctx->state,
           " = _arr_{s}[_i_{s}]",
           (FormatArg[]){{.s = name}, {.s = name}});

    format(ctx->state, ") ", NULL);
    astConstVisit(visitor, node->forStmt.body);

    format(ctx->state, "{<}\n}", NULL);
}

void generateArrayExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    generateManyAstsWithDelim(
        visitor, "{{", ", ", "}", node->arrayExpr.elements);
}

void generateArrayDeclaration(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    if (isSliceType(type)) {
        format(state, "typedef struct ", NULL);
        writeTypename(context, type);
        format(state, " {{{>}\n", NULL);
        generateTypeUsage(context, type->array.elementType);
        format(state, " *data;\n", NULL);
        format(state, "u64 len;{<}\n} ", NULL);
        writeTypename(context, type);
    }
    else {
        format(state, "typedef ", NULL);
        generateTypeUsage(context, type->array.elementType);
        format(state, " ", NULL);
        writeTypename(context, type);
        format(state, "[{u64}]", (FormatArg[]){{.u64 = type->array.len}});
    }

    format(state, ";\n", NULL);
    generateArrayDelete(context, type);
}

void checkArrayType(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *element = evalType(visitor, node->arrayType.elementType);

    u64 size = UINT64_MAX;
    if (node->arrayType.dim) {
        // TODO evaluate len
        evalType(visitor, node->arrayType.dim);
        csAssert0(node->arrayType.dim->tag == astIntegerLit);
        size = node->arrayType.dim->intLiteral.value;
    }

    node->type = makeArrayType(ctx->typeTable, element, size);
}

void checkArrayExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    u64 count = 0;
    const Type *elementType = NULL;
    for (AstNode *elem = node->arrayExpr.elements; elem;
         elem = elem->next, count++) {
        const Type *type = evalType(visitor, elem);
        if (elementType == NULL) {
            elementType = type;
            continue;
        }

        if (!isTypeAssignableFrom(elementType, type)) {
            logError(ctx->L,
                     &elem->loc,
                     "inconsistent array types in array, expecting '{t}', "
                     "got '{t}'",
                     (FormatArg[]){{.t = elementType}, {.t = type}});
        }
    }
    if (elementType == NULL) {
        node->type =
            makeArrayType(ctx->typeTable, makeAutoType(ctx->typeTable), 0);
    }
    else {
        node->type = makeArrayType(ctx->typeTable, elementType, count);
    }
}

void evalArrayExpr(AstVisitor *visitor, AstNode *node)
{
    AstNode *arg = node->arrayExpr.elements;
    for (; arg; arg = arg->next) {
        if (!evaluate(visitor, arg)) {
            node->tag = astError;
            return;
        }
    }
}
