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

static void generateEnumDefinition(CCodegenContext *context, const Type *type)
{
    FormatState *state = context->base.state;
    format(state, "enum {{{>}\n", NULL);
    for (u64 i = 0; i < type->tEnum.count; i++) {
        const EnumOption *option = &type->tEnum.options[i];
        if (i != 0)
            format(state, "\n", NULL);
        writeEnumPrefix(state, type);
        format(state,
               "_{s} = {u64},",
               (FormatArg[]){{.s = option->name}, {.u64 = option->value}});
    }
    format(state, "{<}\n};\n", NULL);

    format(state, "typedef ", NULL);
    writeTypename(state, type->tEnum.base);
    format(state, " ", NULL);
    writeTypename(state, type);
    format(state, ";\n", NULL);

    format(state, "const __cxy_enum_names_t ", NULL);
    writeEnumPrefix(state, type);
    format(state, "_enum_names[] = {{{>}\n", NULL);

    for (u64 i = 0; i < type->tEnum.count; i++) {
        const EnumOption *option = &type->tEnum.options[i];
        if (i != 0)
            format(state, "\n", NULL);
        format(state,
               "{{.value = {u64}, .name = \"{s}\"},",
               (FormatArg[]){{.u64 = option->value}, {.s = option->name}});
    }
    format(state, "{<}\n}", NULL);
}

static void generateStructDefinition(CCodegenContext *context, const Type *type)
{
    FormatState *state = context->base.state;
    format(state, "typedef struct {{{>}\n", NULL);
    for (u64 i = 0; i < type->tStruct.fieldsCount; i++) {
        const StructField *field = &type->tStruct.fields[i];
        if (i != 0)
            format(state, "\n", NULL);
        generateTypeUsage(context, field->type);
        format(state, " {s};", (FormatArg[]){{.s = field->name}});
    }
    format(state, "{<}\n};\n", NULL);
    format(state, "void ", NULL);
    writeTypename(state, type);
    format(state, "op_str0(", NULL);
    writeTypename(state, type);
    format(state, " *this, __cxy_string_builder_t *sb) {{{>}\n", NULL);
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
    if (type->array.size != UINT64_MAX) {
        format(state, "[{u64}]", (FormatArg[]){{.u64 = type->array.size}});
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
        break;
    case typFunc:
        generateFuncDeclaration(context, type);
        break;
    case typTuple:
        generateTupleDefinition(context, type);
        break;
    case typEnum:
        generateEnumDefinition(context, type);
        break;
    case typStruct:
        generateStructDefinition(context, type);
        break;
    default:
        return;
    }

    format(state, ";\n", NULL);
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
           "/* --------------------- epilogue.cxy.c --------------*/\n"
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
