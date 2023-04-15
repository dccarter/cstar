/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-02
 */

#include "ccodegen.h"
#include "lang/ttable.h"

#include <string.h>

static void generateStringExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    CCodegenContext *cctx = getConstAstVisitorContext(visitor);

    const Type *type = node->type;
    cstring namespace = type->namespace ?: "";

    switch (type->tag) {
    case typString:
        if (node->tag == astStringLit) {
            u64 len = strlen(node->stringLiteral.value);
            if (len)
                format(
                    ctx->state,
                    "__cxy_string_builder_append_cstr0(&sb, \"{s}\", {u64});\n",
                    (FormatArg[]){{.s = node->stringLiteral.value},
                                  {.u64 = len}});
        }
        else {
            format(ctx->state, "__cxy_string_builder_append_cstr1(&sb, ", NULL);
            astConstVisit(visitor, node);
            format(ctx->state, ");\n", NULL);
        }
        break;
    case typPrimitive:
        switch (type->primitive.id) {
        case prtBool:
            format(ctx->state, "__cxy_string_builder_append_bool(&sb, ", NULL);
            break;
        case prtChar:
            format(ctx->state, "__cxy_string_builder_append_char(&sb, ", NULL);
            break;
#define f(I, ...) case prt##I:
            INTEGER_TYPE_LIST(f)
            format(ctx->state, "__cxy_string_builder_append_int(&sb, ", NULL);
            break;
#undef f
        case prtF32:
        case prtF64:
            format(ctx->state, "__cxy_string_builder_append_float(&sb, ", NULL);
            break;
        default:
            break;
        }
        astConstVisit(visitor, node);
        format(ctx->state, ");\n", NULL);
        break;

    case typTuple:
        format(
            ctx->state, "__cxy_string_builder_append_char(&sb, '(');\n", NULL);
        for (u64 i = 0; i < type->tuple.count; i++) {
            // Create a temporary member access expression
            AstNode member = {.tag = astIntegerLit,
                              .type = makePrimitiveType(cctx->table, prtI32),
                              .intLiteral.value = i};
            AstNode arg = {
                .tag = astMemberExpr,
                .type = type->tuple.members[i],
                .memberExpr = {.target = (AstNode *)node, .member = &member}};

            if (i != 0)
                format(ctx->state,
                       "__cxy_string_builder_append_cstr0(&sb, \", \", 2);\n",
                       NULL);

            generateStringExpr(visitor, &arg);
        }
        format(
            ctx->state, "__cxy_string_builder_append_char(&sb, ')');\n", NULL);
        break;

    case typArray:
        format(
            ctx->state, "__cxy_string_builder_append_char(&sb, '[');\n", NULL);
        for (u64 i = 0; i < type->array.size; i++) {
            // Create a temporary member access expression
            AstNode index = {.tag = astIntegerLit,
                             .type = makePrimitiveType(cctx->table, prtI32),
                             .intLiteral.value = i};
            AstNode arg = {
                .tag = astIndexExpr,
                .type = type->array.elementType,
                .indexExpr = {.target = (AstNode *)node, .index = &index}};

            if (i != 0)
                format(ctx->state,
                       "__cxy_string_builder_append_cstr0(&sb, \", \", 2);\n",
                       NULL);

            generateStringExpr(visitor, &arg);
        }
        format(
            ctx->state, "__cxy_string_builder_append_char(&sb, ']');\n", NULL);
        break;

    case typPointer: {
        format(
            ctx->state, "__cxy_string_builder_append_char(&sb, '&');\n", NULL);
        AstNode arg = {.tag = astUnaryExpr,
                       .type = type->pointer.pointed,
                       .unaryExpr = {.operand = (AstNode *)node,
                                     .op = opDeref,
                                     .isPrefix = true}};
        generateStringExpr(visitor, &arg);
        break;
    case typEnum:
        format(
            ctx->state,
            "__cxy_string_builder_append_cstr0(&sb, \"{s}{s}{s}.\", {u64});\n",
            (FormatArg[]){{.s = namespace},
                          {.s = type->namespace ? "." : ""},
                          {.s = type->name},
                          {.u64 = strlen(type->name) + strlen(namespace) + 1}});
        format(ctx->state, "__cxy_string_builder_append_cstr1(&sb, ", NULL);
        format(ctx->state,
               "__cxy_enum_find_name(",
               (FormatArg[]){{.s = namespace}, {.s = type->name}});
        writeEnumPrefix(ctx->state, type);
        format(ctx->state,
               "_enum_names, ",
               (FormatArg[]){{.s = namespace}, {.s = type->name}});
        astConstVisit(visitor, node);
        format(ctx->state, "));\n", NULL);
        break;
    }
    default:
        format(ctx->state,
               "__cxy_string_builder_append_cstr0(&sb, \"null\", 4);\n",
               NULL);
        break;
    }
}

void cCodegenStringExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *part = node->stringExpr.parts;

    format(ctx->state,
           "({{{>}\n__cxy_string_builder_t sb = {{};\n"
           "__cxy_string_builder_init(&sb);\n",
           NULL);

    for (; part; part = part->next) {
        generateStringExpr(visitor, part);
    }

    format(ctx->state, "__cxy_string_builder_release(&sb);{<}\n})", NULL);
}