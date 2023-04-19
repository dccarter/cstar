//
// Created by Carter on 2023-04-18.
//

#include "lang/codegen.h"
#include "lang/semantics.h"

#include "lang/ttable.h"

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
    format(ctx->state, ";\n", NULL);

    // create index variable
    format(ctx->state, "u64 _i_{s} = 0;\n", (FormatArg[]){{.s = name}});

    // Create actual loop variable
    generateTypeUsage(ctx, range->type->array.elementType);
    format(ctx->state, " ", NULL);
    astConstVisit(visitor, var->varDecl.names);
    format(ctx->state, " = _arr_{s}[0];\n", (FormatArg[]){{.s = name}});

    format(ctx->state,
           "for (; _i_{s} < {u64}; _i_{s}++, ",
           (FormatArg[]){
               {.s = name}, {.u64 = range->type->array.size}, {.s = name}});
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
    format(state, "typedef ", NULL);
    generateTypeUsage(context, type->array.elementType);
    format(state, " ", NULL);
    writeTypename(context, type);
    if (type->array.size != UINT64_MAX) {
        format(state, "[{u64}]", (FormatArg[]){{.u64 = type->array.size}});
    }
    else
        format(state, "[]", NULL);
}

void checkArrayType(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *element = evalType(visitor, node->arrayType.elementType);

    u64 size = UINT64_MAX;
    if (node->arrayType.dim) {
        // TODO evaluate size
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

