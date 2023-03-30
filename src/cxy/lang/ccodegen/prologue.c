//
// Created by Carter on 2023-03-29.
//

#include "ccodegen.h"
#include "lang/ttable.h"

#include "core/alloc.h"

#define CXY_ANONYMOUS_FUNC "__cxy_anonymous_func"
#define CXY_ANONYMOUS_TUPLE "__cxy_anonymous_tuple"
#define CXY_ANONYMOUS_STRUCT "__cxy_anonymous_struct"

static void writeTypename(FormatState *state, const Type *type)
{
    if (type->name) {
        format(state, "{s}", (FormatArg[]){{.s = type->name}});
    }
    else {
        switch (type->tag) {
        case typFunc:
            format(state,
                   CXY_ANONYMOUS_FUNC "{u64}_t",
                   (FormatArg[]){{.u64 = type->index}});
            break;
        case typTuple:
            format(state,
                   CXY_ANONYMOUS_TUPLE "{u64}_t",
                   (FormatArg[]){{.u64 = type->index}});
            break;
        case typStruct:
            format(state,
                   CXY_ANONYMOUS_STRUCT "{u64}_t",
                   (FormatArg[]){{.u64 = type->index}});
            break;
        default:
            unreachable();
        }
    }
}

static void generateTypeUsage(CCodegenContext *ctx, const Type *type)
{
    FormatState *state = ctx->base.state;

    switch (type->tag) {
    case typVoid:
        format(state, "void", NULL);
        break;
    case typString:
        format(state, "string", NULL);
        break;

    case typPrimitive:
        format(state,
               "{s}",
               (FormatArg[]){{.s = getPrimitiveTypeName(type->primitive.id)}});
        break;

    case typPointer:
        if (type->pointer.isConst)
            format(state, "const ", NULL);
        generateTypeUsage(ctx, type->pointer.pointed);
        format(state, " *", NULL);
        break;

    case typArray:
        generateTypeUsage(ctx, type->array.elementType);
        for (u64 i = 0; i < type->array.arity; i++) {
            if (type->array.indexes[i] != UINT64_MAX)
                format(state,
                       "[{u64}]",
                       (FormatArg[]){{.u64 = type->array.indexes[i]}});
            else
                format(state, "[]", NULL);
        }
        break;

    case typTuple:
    case typStruct:
    case typFunc:
        writeTypename(state, type);
        break;
    default:
        break;
    }
}

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

static void generateType(CCodegenContext *context, const Type *type)
{
    FormatState *state = context->base.state;

    switch (type->tag) {
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

static void generateProgram(ConstAstVisitor *visitor, const AstNode *node)
{
    CCodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->base.state,
           "#include <stdio.h>\n"
           "#include <stdlib.h>\n"
           "#include <stdint.h>\n"
           "\n"
           "\n"
           "typedef uint8_t u8;\n"
           "typedef uint16_t u16;\n"
           "typedef uint32_t u32;\n"
           "typedef uint64_t u64;\n"
           "typedef int8_t i8;\n"
           "typedef int16_t i16;\n"
           "typedef int32_t i32;\n"
           "typedef int64_t i64;\n"
           "\n"
           "typedef int8_t f32;\n"
           "typedef int8_t f64;\n"
           "\n"
           "typedef const char* string;\n"
           "\n",
           NULL);

    generateAllTypes(ctx);

    generateManyAsts(visitor, "\n\n", node->program.decls);
}

static void generateIdentifier(ConstAstVisitor *visitor, const AstNode *node)
{
    CodeGenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "{s}", (FormatArg[]){{.s = node->ident.value}});
}

static void generateFunc(ConstAstVisitor *visitor, const AstNode *node)
{
    CodeGenContext *ctx = getConstAstVisitorContext(visitor);
}

static void generateTypeDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    CCodegenContext *ctx = getConstAstVisitorContext(visitor);
    generateTypeUsage(ctx, node->type);
}

static void generateVariable(ConstAstVisitor *visitor, const AstNode *node)
{
    CCodegenContext *ctx = getConstAstVisitorContext(visitor);
    FormatState *state = ctx->base.state;

    if (node->tag == astConstDecl)
        format(state, "const ", NULL);
    generateTypeUsage(ctx, node->type);

    format(state, " ", NULL);
    astConstVisit(visitor, node->varDecl.names);

    if (node->varDecl.init) {
        format(state, " = ", NULL);
        astConstVisit(visitor, node->varDecl.init);
    }
    format(state, ";", NULL);
}

static void generateLiteral(ConstAstVisitor *visitor, const AstNode *node)
{
    CCodegenContext *ctx = getConstAstVisitorContext(visitor);
    FormatState *state = ctx->base.state;

    switch (node->tag) {
    case astNullLit:
        format(state, "nullptr", NULL);
        break;
    case astBoolLit:
        format(
            state,
            "{s}",
            (FormatArg[]){{.s = node->boolLiteral.value ? "true" : "false"}});
        break;
    case astCharLit:
        format(state, "'{c}'", (FormatArg[]){{.c = node->charLiteral.value}});
        break;
    case astIntegerLit:
        format(state,
               "{s}{u64}",
               (FormatArg[]){{.s = node->intLiteral.hasMinus ? "-" : ""},
                             {.u64 = node->intLiteral.value}});
        break;
    case astFloatLit:
        format(
            state, "{f64}", (FormatArg[]){{.f64 = node->floatLiteral.value}});
        break;
    case astStringLit:
        format(
            state, "\"{s}\"", (FormatArg[]){{.s = node->stringLiteral.value}});
        break;
    default:
        break;
    }
}

void cCodegenPrologue(CCodegenContext *context, const AstNode *prog)
{
    // clang-format off
    ConstAstVisitor visitor = makeConstAstVisitor(context,
    {
        [astProgram] = generateProgram,
        [astIdentifier] = generateIdentifier,
        [astNullLit] = generateLiteral,
        [astBoolLit] = generateLiteral,
        [astCharLit] = generateLiteral,
        [astIntegerLit] = generateLiteral,
        [astFloatLit] = generateLiteral,
        [astStringLit] = generateLiteral,
        [astFuncDecl] = generateFunc,
        [astVarDecl] = generateVariable,
        [astConstDecl] = generateVariable,
        [astTypeDecl] = generateTypeDecl
    },

    .fallback = generateCCodeFallback);

    astConstVisit(&visitor, prog);
}
