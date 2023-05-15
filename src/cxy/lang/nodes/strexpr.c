/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-17
 */

#include "lang/codegen.h"
#include "lang/semantics.h"
#include "lang/ttable.h"

#include <core/sb.h>

#include <string.h>

void evalStringConcatenation(SemanticsContext *ctx,
                             AstNode *node,
                             AstNode *lhs,
                             AstNode *rhs)
{
    StringBuilder sb;
    stringBuilderInit(&sb);
    stringBuilderAppendCstr1(&sb, lhs->stringLiteral.value);
    switch (rhs->tag) {
    case astStringLit:
        stringBuilderAppendCstr1(&sb, rhs->stringLiteral.value);
        break;
    case astIntegerLit:
        if (rhs->intLiteral.hasMinus)
            stringBuilderAppendChar(&sb, '-');
        stringBuilderAppendInt(&sb, rhs->intLiteral.value);
        break;
    case astFloatLit:
        stringBuilderAppendFloat(&sb, rhs->floatLiteral.value);
        break;
    case astCharLit:
        stringBuilderAppendChar(&sb, rhs->charLiteral.value);
        break;
    case astBoolLit:
        stringBuilderAppendBool(&sb, rhs->boolLiteral.value);
        break;
    default:
        unreachable("INVALID operand");
    }

    char *str = stringBuilderRelease(&sb);
    node->tag = astStringLit;
    node->stringLiteral.value = makeString(ctx->strPool, str);
    free(str);
}

void stringBuilderAppend(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);

    const Type *type = node->type;
    cstring namespace = type->namespace ?: "";
    cstring scopeOp = type->namespace ? ":" : "";
    cstring name = type->name ?: "";
    u64 scopedNameLen = strlen(name) + strlen(namespace) + strlen(scopeOp);

    switch (type->tag) {
    case typString:
        if (node->tag == astStringLit) {
            u64 len = strlen(node->stringLiteral.value);
            if (len)
                format(ctx->state,
                       "__cxy_builtins_string_builder_append_cstr0(&sb, "
                       "\"{s}\", {u64});\n",
                       (FormatArg[]){{.s = node->stringLiteral.value},
                                     {.u64 = len}});
        }
        else {
            format(ctx->state,
                   "__cxy_builtins_string_builder_append_cstr1(&sb, ",
                   NULL);
            astConstVisit(visitor, node);
            format(ctx->state, ");\n", NULL);
        }
        break;
    case typPrimitive:
        switch (type->primitive.id) {
        case prtBool:
            format(ctx->state,
                   "__cxy_builtins_string_builder_append_bool(&sb, ",
                   NULL);
            break;
        case prtChar:
            format(ctx->state,
                   "__cxy_builtins_string_builder_append_char(&sb, ",
                   NULL);
            break;
#define f(I, ...) case prt##I:
            INTEGER_TYPE_LIST(f)
            format(ctx->state,
                   "__cxy_builtins_string_builder_append_int(&sb, (i64)",
                   NULL);
            break;
#undef f
        case prtF32:
        case prtF64:
            format(ctx->state,
                   "__cxy_builtins_string_builder_append_float(&sb, ",
                   NULL);
            break;
        default:
            break;
        }
        astConstVisit(visitor, node);
        format(ctx->state, ");\n", NULL);
        break;

    case typTuple:
        format(ctx->state,
               "__cxy_builtins_string_builder_append_char(&sb, '(');\n",
               NULL);
        for (u64 i = 0; i < type->tuple.count; i++) {
            // Create a temporary member access expression
            AstNode member = {.tag = astIntegerLit,
                              .type = getPrimitiveType(ctx->types, prtI32),
                              .intLiteral.value = i};
            AstNode arg = {
                .tag = astMemberExpr,
                .type = type->tuple.members[i],
                .memberExpr = {.target = (AstNode *)node, .member = &member}};

            if (i != 0)
                format(ctx->state,
                       "__cxy_builtins_string_builder_append_cstr0(&sb, \", "
                       "\", 2);\n",
                       NULL);

            stringBuilderAppend(visitor, &arg);
        }
        format(ctx->state,
               "__cxy_builtins_string_builder_append_char(&sb, ')');\n",
               NULL);
        break;

    case typArray:
        format(ctx->state,
               "__cxy_builtins_string_builder_append_char(&sb, '[');\n",
               NULL);
        format(ctx->state, "for (u64 __cxy_i = 0; __cxy_i < ", NULL);
        if (type->array.len == UINT64_MAX) {
            astConstVisit(visitor, node);
            format(ctx->state, "->len", NULL);
        }
        else {
            format(ctx->state, "sizeof__(", NULL);
            writeTypename(ctx, type);
            format(ctx->state, ")", NULL);
        }

        format(ctx->state, "; __cxy_i++) {{{>}\n", NULL);
        format(ctx->state,
               "if (__cxy_i) __cxy_builtins_string_builder_append_cstr0(&sb, "
               "\", \", 2);\n",
               NULL);
        {
            AstNode index = {.tag = astIdentifier,
                             .type = getPrimitiveType(ctx->types, prtI32),
                             .ident.value = "__cxy_i"};
            AstNode arg = {
                .tag = astIndexExpr,
                .type = type->array.elementType,
                .indexExpr = {.target = (AstNode *)node, .index = &index}};

            stringBuilderAppend(visitor, &arg);
        }
        format(ctx->state, "{<}\n}\n", NULL);
        format(ctx->state,
               "__cxy_builtins_string_builder_append_char(&sb, ']');\n",
               NULL);
        break;

    case typPointer: {
        AstNode arg = {.tag = astUnaryExpr,
                       .type = type->pointer.pointed,
                       .unaryExpr = {.operand = (AstNode *)node,
                                     .op = opDeref,
                                     .isPrefix = true}};
        stringBuilderAppend(visitor, &arg);
        break;
    }
    case typEnum:
        format(ctx->state,
               "__cxy_builtins_string_builder_append_cstr1(&sb, ",
               NULL);
        writeEnumPrefix(ctx, type);
        format(ctx->state, "__get_name(", NULL);
        astConstVisit(visitor, node);
        format(ctx->state, "));\n", NULL);
        break;

    case typStruct:
        format(ctx->state,
               "__cxy_builtins_string_builder_append_cstr1(&sb, ",
               NULL);
        writeTypename(ctx, type);
        format(ctx->state, "__op_str(&", NULL);
        astConstVisit(visitor, node);
        format(ctx->state, "));\n", NULL);
        break;
    default:
        break;
    }
}

void checkStringExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *part = node->stringExpr.parts;
    for (; part; part = part->next) {
        part->type = evalType(visitor, part);
        if (part->type == makeErrorType(ctx->typeTable)) {
            node->type = makeErrorType(ctx->typeTable);
            return;
        }
    }

    node->type = makeStringType(ctx->typeTable);
}

void generateStringExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *part = node->stringExpr.parts;

    format(ctx->state,
           "({{{>}\n__cxy_builtins_string_builder_t sb = {{};\n"
           "__cxy_builtins_string_builder_init(&sb);\n",
           NULL);

    for (; part; part = part->next) {
        // skip empty string literals
        if (nodeIs(part, StringLit) && part->stringLiteral.value[0] == '\0')
            continue;
        stringBuilderAppend(visitor, part);
    }

    format(
        ctx->state, "__cxy_builtins_string_builder_release(&sb);{<}\n})", NULL);
}
