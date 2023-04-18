//
// Created by Carter on 2023-03-29.
//

#include "lang/codegen.h"
#include "lang/ttable.h"

#include "core/alloc.h"

#define CXY_PROLOGUE_SRC_FILE CXY_SOURCE_LANG_DIR "/ccodegen/prologue.cxy.c"

static void generateTupleDefinition(CodegenContext *context, const Type *type)
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
}

static void generateEnumDefinition(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    format(state, "enum {{{>}\n", NULL);
    for (u64 i = 0; i < type->tEnum.count; i++) {
        const EnumOption *option = &type->tEnum.options[i];
        if (i != 0)
            format(state, "\n", NULL);
        writeEnumPrefix(context, type);
        format(state,
               "_{s} = {u64},",
               (FormatArg[]){{.s = option->name}, {.u64 = option->value}});
    }
    format(state, "{<}\n};\n", NULL);

    format(state, "typedef ", NULL);
    writeTypename(context, type->tEnum.base);
    format(state, " ", NULL);
    writeTypename(context, type);
    format(state, ";\n", NULL);

    format(state, "const cxy_enum_names_t ", NULL);
    writeEnumPrefix(context, type);
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

static void generateStructDefinition(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    format(state, "struct ", NULL);
    writeTypename(context, type);
    format(state, " {{{>}\n", NULL);
    if (type->tStruct.base) {
        writeTypename(context, type->tStruct.base);
        format(state, " super;\n", NULL);
    }

    for (u64 i = 0; i < type->tStruct.fieldsCount; i++) {
        const StructField *field = &type->tStruct.fields[i];
        if (field->type->tag == typFunc)
            continue;

        if (i != 0)
            format(state, "\n", NULL);

        generateTypeUsage(context, field->type);
        format(state, " {s};", (FormatArg[]){{.s = field->name}});
    }
    format(state, "{<}\n}", NULL);
}

static void generateFuncDeclaration(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    const AstNode *parent = type->func.decl->parentScope;

    format(state, ";\n", NULL);
    generateTypeUsage(context, type->func.retType);
    format(state, " ", NULL);
    writeTypename(context, parent->type);
    format(state, "__{s}", (FormatArg[]){{.s = type->name}});
    format(state, "(", NULL);
    if (type->flags & flgConst)
        format(state, "const ", NULL);
    writeTypename(context, parent->type);
    format(state, " *", NULL);

    for (u64 i = 0; i < type->func.paramsCount; i++) {
        format(state, ", ", NULL);
        generateTypeUsage(context, type->func.params[i]);
    }
    format(state, ")", NULL);
}

static void generateFuncType(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    const AstNode *parent =
        type->func.decl ? type->func.decl->parentScope : NULL;
    bool isMember = parent && parent->tag == astStructDecl;

    format(state, "typedef ", NULL);
    generateTypeUsage(context, type->func.retType);
    format(state, "(*", NULL);
    if (isMember) {
        writeTypename(context, parent->type);
        format(state, "__", NULL);
    }
    writeTypename(context, type);
    format(state, ")(", NULL);
    if (isMember) {
        if (type->flags & flgConst)
            format(state, "const ", NULL);
        writeTypename(context, parent->type);
        format(state, " *this", NULL);
    }

    for (u64 i = 0; i < type->func.paramsCount; i++) {
        if (isMember || i != 0)
            format(state, ", ", NULL);
        generateTypeUsage(context, type->func.params[i]);
    }
    format(state, ")", NULL);
    if (isMember)
        generateFuncDeclaration(context, type);
}

static void generateArrayDeclaration(CodegenContext *context, const Type *type)
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

static void generateType(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;

    switch (type->tag) {
    case typArray:
        generateArrayDeclaration(context, type);
        break;
    case typFunc:
        generateFuncType(context, type);
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

static void generateStructTypedef(CodegenContext *ctx, const Type *type)
{
    FormatState *state = ctx->state;
    format(state, "typedef struct ", NULL);
    writeTypename(ctx, type);
    format(state, " ", NULL);
    writeTypename(ctx, type);
    format(state, ";\n", NULL);
}

void generateAllTypes(CodegenContext *ctx)
{
    u64 typesCount = getTypesCount(ctx->types);
    const Type **types = mallocOrDie(sizeof(Type *) * typesCount);
    u64 sorted = sortedByInsertionOrder(ctx->types, types, typesCount);

    for (u64 i = 0; i < sorted; i++) {
        if (types[i]->tag == typStruct)
            generateStructTypedef(ctx, types[i]);
    }

    for (u64 i = 0; i < sorted; i++) {
        generateType(ctx, types[i]);
    }
}

static void programPrologue(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);

    size_t bytes = 0;
    format(ctx->state,
           "/**\n"
           " * Generated from cxy compile\n"
           " */\n"
           "\n"
           "/* --------------------- epilogue.cxy.c --------------*/\n"
           "\n",
           NULL);
    append(ctx->state, readFile(CXY_PROLOGUE_SRC_FILE, &bytes), bytes);

    format(ctx->state,
           "\n"
           "/* --------------------- Generated PROLOGUE --------------*/\n"
           "\n",
           NULL);
    generateAllTypes(ctx);
}

void codegenPrologue(CodegenContext *context, const AstNode *prog)
{
    // clang-format off
    ConstAstVisitor visitor = makeConstAstVisitor(context,
    {
        [astProgram] = programPrologue,
    });

    astConstVisit(&visitor, prog);
}
