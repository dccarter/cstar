//
// Created by Carter on 2023-03-29.
//

#include "ccodegen.h"
#include "lang/ttable.h"

#include "core/alloc.h"

#define CXY_PROLOGUE_SRC_FILE CXY_SOURCE_LANG_DIR "/ccodegen/prologue.cxy.c"

static void generateTupleDefinition(CCodegenContext *context, const Type *type)
{
    FormatState *state = context->base.state;

    format(state, "typedef struct {{{>}\n", NULL);
    for (u64 i = 0; i < type->tuple.count; i++) {
        if (i != 0)
            format(state, "\n", NULL);
        generateTypeUsage(context, type->tuple.members[i]);
        format(state, " _{u64};", (FormatArg[]){{.u64 = i}});
    }
    format(state, "{<}\n} ", NULL);
    writeTypename(state, type);
}

static void generateFuncDeclaration(CCodegenContext *context, const Type *type)
{
    FormatState *state = context->base.state;
    format(state, "typedef ", NULL);
    generateTypeUsage(context, type->func.retType);
    format(state, "(*", NULL);
    writeTypename(state, type);
    format(state, ")(", NULL);
    for (u64 i = 0; i < type->func.paramsCount; i++) {
        if (i != 0)
            format(state, ", ", NULL);
        generateTypeUsage(context, type->func.params[i]);
    }
    format(state, ")", NULL);
}

static void generateArrayDeclaration(CCodegenContext *context, const Type *type)
{
    FormatState *state = context->base.state;
    format(state, "typedef ", NULL);
    generateTypeUsage(context, type->array.elementType);
    format(state, " ", NULL);
    writeTypename(state, type);
    if (type->array.arity) {
        for (u64 i = 0; i < type->array.arity; i++) {
            if (type->array.indexes[i] == UINT64_MAX)
                format(state, "[]", NULL);
            else
                format(state,
                       "[{u64}]",
                       (FormatArg[]){{.u64 = type->array.indexes[i]}});
        }
    }
    else
        format(state, "[]", NULL);
}

static void generateType(CCodegenContext *context, const Type *type)
{
    FormatState *state = context->base.state;

    switch (type->tag) {
    case typArray:
        generateArrayDeclaration(context, type);
        format(state, ";\n", NULL);
        break;
    case typFunc:
        if (type->name == NULL) {
            generateFuncDeclaration(context, type);
            format(state, ";\n", NULL);
        }
        break;
    case typTuple:
        generateTupleDefinition(context, type);
        format(state, ";\n", NULL);
        break;
    default:
        break;
    }
}

void generateAllTypes(CCodegenContext *ctx)
{
    u64 typesCount = getTypesCount(ctx->table);
    const Type **types = mallocOrDie(sizeof(Type *) * typesCount);
    u64 sorted = sortedByInsertionOrder(ctx->table, types, typesCount);

    for (u64 i = 0; i < sorted; i++) {
        generateType(ctx, types[i]);
    }
}

static void programPrologue(ConstAstVisitor *visitor, const AstNode *node)
{
    CCodegenContext *ctx = getConstAstVisitorContext(visitor);

    size_t bytes = 0;
    format(ctx->base.state,
           "/**\n"
           " * Generated from cxy compile\n"
           " */\n"
           "\n"
           "/* --------------------- prologue.cxy.c --------------*/\n"
           "\n",
           NULL);
    append(ctx->base.state, readFile(CXY_PROLOGUE_SRC_FILE, &bytes), bytes);

    format(ctx->base.state,
           "\n"
           "/* --------------------- Generated PROLOGUE --------------*/\n"
           "\n",
           NULL);
    generateAllTypes(ctx);
}

void cCodegenPrologue(CCodegenContext *context, const AstNode *prog)
{
    // clang-format off
    ConstAstVisitor visitor = makeConstAstVisitor(context,
    {
        [astProgram] = programPrologue,
    });

    astConstVisit(&visitor, prog);
}
