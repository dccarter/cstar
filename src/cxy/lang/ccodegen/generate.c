//
// Created by Carter on 2023-03-29.
//
#include "lang/ccodegen/ccodegen.h"
#include "lang/codegen.h"

#define CXY_ANONYMOUS_FUNC "__cxy_anonymous_func"
#define CXY_ANONYMOUS_TUPLE "__cxy_anonymous_tuple"
#define CXY_ANONYMOUS_STRUCT "__cxy_anonymous_struct"
#define CXY_ANONYMOUS_ARRAY "__cxy_anonymous_array"

void generateCCodeFallback(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state,
           "/* <unsupported AST tag {u32}> */",
           (FormatArg[]){{.u32 = node->tag}});
}

void writeTypename(FormatState *state, const Type *type)
{
    cstring ns = type->namespace ? type->namespace : "";
    if (type->name) {
        format(state, "{s}{s}", (FormatArg[]){{.s = ns}, {.s = type->name}});
    }
    else {
        switch (type->tag) {
        case typFunc:
            format(state,
                   CXY_ANONYMOUS_FUNC "{s}_{u64}_t",
                   (FormatArg[]){{.s = ns}, {.u64 = type->index}});
            break;
        case typTuple:
            format(state,
                   CXY_ANONYMOUS_TUPLE "{s}_{u64}_t",
                   (FormatArg[]){{.s = ns}, {.u64 = type->index}});
            break;
        case typStruct:
            format(state,
                   CXY_ANONYMOUS_STRUCT "{s}_{u64}_t",
                   (FormatArg[]){{.s = ns}, {.u64 = type->index}});
            break;
        case typArray:
            format(state,
                   CXY_ANONYMOUS_ARRAY "{s}_{u64}_t",
                   (FormatArg[]){{.s = ns}, {.u64 = type->index}});
            break;
        default:
            unreachable();
        }
    }
}

void generateTypeUsage(CCodegenContext *ctx, const Type *type)
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
        if (type->flags & flgConst)
            format(state, "const ", NULL);
        generateTypeUsage(ctx, type->pointer.pointed);
        format(state, "*", NULL);
        break;

    case typArray:
    case typTuple:
    case typStruct:
    case typFunc:
        writeTypename(state, type);
        break;
    default:
        break;
    }
}

void generateCode(FormatState *state, TypeTable *table, const AstNode *prog)
{
    CCodegenContext context = {.base = {.state = state}, .table = table};

    cCodegenPrologue(&context, prog);
    cCodegenEpilogue(&context, prog);
}
