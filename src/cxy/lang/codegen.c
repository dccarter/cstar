/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-17
 */

#include "codegen.h"

#define CXY_ANONYMOUS_FUNC "cxy_anonymous_func"
#define CXY_ANONYMOUS_TUPLE "cxy_anonymous_tuple"
#define CXY_ANONYMOUS_STRUCT "cxy_anonymous_struct"
#define CXY_ANONYMOUS_ARRAY "cxy_anonymous_array"
#define CXY_ANONYMOUS_ENUM "cxy_anonymous_enum"

void generateFallback(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state,
           "/* <unsupported AST tag {u32}> */",
           (FormatArg[]){{.u32 = node->tag}});
}

void writeNamespace(CodegenContext *ctx, cstring sep)
{
    if (ctx->namespace) {
        format(ctx->state,
               "{s}{s}",
               (FormatArg[]){{.s = ctx->namespace}, {.s = sep ?: "__"}});
    }
}

void writeEnumPrefix(CodegenContext *ctx, const Type *type)
{
    FormatState *state = ctx->state;
    csAssert0(type->tag == typEnum);

    writeNamespace(ctx, NULL);
    if (type->name) {
        format(state, "{s}", (FormatArg[]){{.s = type->name}});
    }
    else {
        format(state,
               CXY_ANONYMOUS_ENUM "{u64}",
               (FormatArg[]){{.u64 = type->index}});
    }
}

void writeTypename(CodegenContext *ctx, const Type *type)
{
    FormatState *state = ctx->state;

    writeNamespace(ctx, NULL);

    if (type->name) {
        if (type->tag == typFunc)
            format(state, "{s}_t", (FormatArg[]){{.s = type->name}});
        else
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
        case typArray:
            format(state,
                   CXY_ANONYMOUS_ARRAY "{u64}_t",
                   (FormatArg[]){{.u64 = type->index}});
            break;
        case typEnum:
            format(state,
                   CXY_ANONYMOUS_ENUM "{u64}_t",
                   (FormatArg[]){{.u64 = type->index}});
            break;
        default:
            unreachable();
        }
    }
}

void generateTypeUsage(CodegenContext *ctx, const Type *type)
{
    FormatState *state = ctx->state;

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
    case typThis:
        writeTypename(ctx, type);
        break;
    default:
        break;
    }
}

void generateManyAsts(ConstAstVisitor *visitor,
                      const char *sep,
                      const AstNode *nodes)
{
    CodegenContext *context = getConstAstVisitorContext(visitor);
    for (const AstNode *node = nodes; node; node = node->next) {
        astConstVisit(visitor, node);
        if (node->next)
            format(context->state, sep, NULL);
    }
}

void generateManyAstsWithDelim(ConstAstVisitor *visitor,
                               const char *open,
                               const char *sep,
                               const char *close,
                               const AstNode *nodes)
{
    CodegenContext *context = getConstAstVisitorContext(visitor);
    format(context->state, open, NULL);
    generateManyAsts(visitor, sep, nodes);
    format(context->state, close, NULL);
}

void generateAstWithDelim(ConstAstVisitor *visitor,
                          const char *open,
                          const char *close,
                          const AstNode *node)
{
    CodegenContext *context = getConstAstVisitorContext(visitor);
    format(context->state, open, NULL);
    astConstVisit(visitor, node);
    format(context->state, close, NULL);
}

void generateManyAstsWithinBlock(ConstAstVisitor *visitor,
                                 const char *sep,
                                 const AstNode *nodes,
                                 bool newLine)
{
    CodegenContext *context = getConstAstVisitorContext(visitor);
    if (!nodes)
        format(context->state, "{{}", NULL);
    else if (!newLine && !nodes->next)
        generateAstWithDelim(visitor, "{{ ", " }", nodes);
    else
        generateManyAstsWithDelim(visitor, "{{{>}\n", sep, "{<}\n}", nodes);
}

void generateCode(FormatState *state,
                  TypeTable *table,
                  StrPool *strPool,
                  const AstNode *prog)
{
    CodegenContext context = {
        .state = state, .types = table, .strPool = strPool};

    codegenPrologue(&context, prog);
    codegenEpilogue(&context, prog);
}
