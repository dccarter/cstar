//
// Created by Carter on 2023-03-29.
//
#include "lang/ccodegen/ccodegen.h"
#include "lang/codegen.h"

#define CXY_ANONYMOUS_FUNC "cxy_anonymous_func"
#define CXY_ANONYMOUS_TUPLE "cxy_anonymous_tuple"
#define CXY_ANONYMOUS_STRUCT "cxy_anonymous_struct"
#define CXY_ANONYMOUS_ARRAY "cxy_anonymous_array"
#define CXY_ANONYMOUS_ENUM "cxy_anonymous_enum"

void generateCCodeFallback(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state,
           "/* <unsupported AST tag {u32}> */",
           (FormatArg[]){{.u32 = node->tag}});
}

void writeEnumPrefix(FormatState *state, const Type *type)
{
    csAssert0(type->tag == typEnum);
    cstring ns = type->namespace ?: "";
    if (type->name) {
        format(state,
               "{s}{s}{s}",
               (FormatArg[]){{.s = ns},
                             {.s = type->namespace ? "__" : ""},
                             {.s = type->name}});
    }
    else {
        format(state,
               CXY_ANONYMOUS_ENUM "{s}__{u64}",
               (FormatArg[]){{.s = ns}, {.u64 = type->index}});
    }
}

void writeTypename(FormatState *state, const Type *type)
{
    cstring ns = type->namespace ?: "";
    if (type->name) {
        if (type->tag == typFunc)
            format(state,
                   "{s}{s}{s}_t",
                   (FormatArg[]){{.s = ns},
                                 {.s = type->namespace ? "__" : ""},
                                 {.s = type->name}});
        else
            format(state,
                   "{s}{s}{s}",
                   (FormatArg[]){{.s = ns},
                                 {.s = type->namespace ? "__" : ""},
                                 {.s = type->name}});
    }
    else {
        switch (type->tag) {
        case typFunc:
            format(state,
                   CXY_ANONYMOUS_FUNC "{s}__{u64}_t",
                   (FormatArg[]){{.s = ns}, {.u64 = type->index}});
            break;
        case typTuple:
            format(state,
                   CXY_ANONYMOUS_TUPLE "{s}__{u64}_t",
                   (FormatArg[]){{.s = ns}, {.u64 = type->index}});
            break;
        case typStruct:
            format(state,
                   CXY_ANONYMOUS_STRUCT "{s}__{u64}_t",
                   (FormatArg[]){{.s = ns}, {.u64 = type->index}});
            break;
        case typArray:
            format(state,
                   CXY_ANONYMOUS_ARRAY "{s}__{u64}_t",
                   (FormatArg[]){{.s = ns}, {.u64 = type->index}});
            break;
        case typEnum:
            format(state,
                   CXY_ANONYMOUS_ENUM "{s}__{u64}_t",
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
    case typEnum:
    case typOpaque:
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

void generateCode(FormatState *state,
                  TypeTable *table,
                  StrPool *strPool,
                  const AstNode *prog)
{
    CCodegenContext context = {
        .base = {.state = state}, .table = table, .strPool = strPool};

    cCodegenPrologue(&context, prog);
    cCodegenEpilogue(&context, prog);
}
