//
// Created by Carter on 2023-03-29.
//

#include "lang/codegen.h"
#include "lang/ttable.h"

#include "core/alloc.h"

#define CXY_PROLOGUE_SRC_FILE CXY_SOURCE_LANG_DIR "/ccodegen/prologue.cxy.c"

static void generateType(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;

    switch (type->tag) {
    case typArray:
        generateArrayDeclaration(context, type);
        break;
    case typFunc:
        generateFunctionTypedef(context, type);
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

void generateAllTypes(CodegenContext *ctx)
{
    u64 typesCount = getTypesCount(ctx->types);
    const Type **types = callocOrDie(1, sizeof(Type *) * typesCount);
    u64 sorted = sortedByInsertionOrder(ctx->types, types, typesCount);

    u64 empty = 0;
    for (u64 i = 0; i < sorted; i++) {
        if (types[i] && types[i]->tag == typStruct)
            generateStructTypedef(ctx, types[i]);
    }

    for (u64 i = 0; i < sorted; i++) {
        if (types[i])
            generateType(ctx, types[i]);
        else
            empty++;
    }

    free(types);
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
