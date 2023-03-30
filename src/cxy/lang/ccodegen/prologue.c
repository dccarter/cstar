//
// Created by Carter on 2023-03-29.
//

#include "ccodegen.h"
#include "lang/ttable.h"

static void declareType(CCodegenContext *ctx, const Type *type)
{
    FormatState *state = ctx->base.state;

    switch (type->tag) {
    case typPrimitive:
        format(state,
               "{s}",
               (FormatArg[]){{.s = getPrimitiveTypeName(type->primitive.id)}});
        break;
    case typTuple:
        format(state, "struct {{{>}\n", NULL);
        for (u64 i = 0; i < type->tuple.count; i++) {
            if (i != 0)
                format(state, "\n", NULL);
            declareType(ctx, type->tuple.members[i]);
            format(state, " _{u64};", (FormatArg[]){{.u64 = i}});
        }
        format(state, "{<}\n}", NULL);
        break;
    case typAlias:
        format(state, "typedef ", NULL);
        declareType(ctx, resolveType(ctx->table, type->alias.aliased));
        format(state, " {s};\n", (FormatArg[]){{.s = type->name}});
        break;
    case typPointer:
        if (type->pointer.isConst)
            format(state, "const ", NULL);
        declareType(ctx, type->pointer.pointed);
        format(state, " *", NULL);
        break;
    case typArray:
        declareType(ctx, type->array.elementType);
        for (u64 i = 0; i < type->array.arity; i++) {
            if (type->array.indexes[i] != UINT64_MAX)
                format(state,
                       "[{u64}]",
                       (FormatArg[]){{.u64 = type->array.indexes[i]}});
            else
                format(state, "[]", NULL);
        }
    default:
        break;
    }
}

static void generateProgram(ConstAstVisitor *visitor, const AstNode *node)
{
    CodeGenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state,
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
    generateManyAsts(visitor, "\n\n", node->program.decls);
}

static void generateFunc(ConstAstVisitor *visitor, const AstNode *node)
{
    CodeGenContext *ctx = getConstAstVisitorContext(visitor);
}

static void generateTypeDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    CCodegenContext *ctx = getConstAstVisitorContext(visitor);
    declareType(ctx, node->type);
}

static void generateVariable(ConstAstVisitor *visitor, const AstNode *node)
{
    CCodegenContext *ctx = getConstAstVisitorContext(visitor);
    declareType(ctx, node->type);
}

void cCodegenPrologue(CCodegenContext *context, const AstNode *prog)
{
    // clang-format off
    ConstAstVisitor visitor = makeConstAstVisitor(context,
    {
        [astProgram] = generateProgram,
        [astFuncDecl] = generateFunc,
        [astVarDecl] = generateVariable,
        [astConstDecl] = generateVariable,
        [astTypeDecl] = generateTypeDecl
    },

    .fallback = generateCCodeFallback);

    astConstVisit(&visitor, prog);
}
