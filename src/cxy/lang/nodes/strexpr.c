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

bool evalStringBuilderAppend(SemanticsContext *ctx,
                             StringBuilder *sb,
                             AstNode *node)
{
    switch (node->tag) {
    case astStringLit:
        stringBuilderAppendCstr1(sb, node->stringLiteral.value);
        break;
    case astIntegerLit:
        if (node->intLiteral.hasMinus)
            stringBuilderAppendChar(sb, '-');
        stringBuilderAppendInt(sb, node->intLiteral.value);
        break;
    case astFloatLit:
        stringBuilderAppendFloat(sb, node->floatLiteral.value);
        break;
    case astCharLit:
        stringBuilderAppendChar(sb, node->charLiteral.value);
        break;
    case astBoolLit:
        stringBuilderAppendBool(sb, node->boolLiteral.value);
        break;
    default:
        logError(ctx->L,
                 &node->loc,
                 "expression cannot be transformed to a string",
                 NULL);
        return false;
    }

    return true;
}

void evalStringConcatenation(SemanticsContext *ctx,
                             AstNode *node,
                             AstNode *lhs,
                             AstNode *rhs)
{
    StringBuilder sb;
    stringBuilderInit(&sb);
    evalStringBuilderAppend(ctx, &sb, lhs);
    csAssert0(evalStringBuilderAppend(ctx, &sb, rhs));
    
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

    case typEnum:
        format(ctx->state,
               "__cxy_builtins_string_builder_append_cstr1(&sb, ",
               NULL);
        writeEnumPrefix(ctx, type);
        format(ctx->state, "__get_name(", NULL);
        astConstVisit(visitor, node);
        format(ctx->state, "));\n", NULL);
        break;

    case typTuple:
    case typArray:
    case typStruct:
        writeTypename(ctx, type);
        format(ctx->state,
               "__toString({s}",
               (FormatArg[]){{.s = (typeIs(type, Pointer) || isSliceType(type))
                                       ? ""
                                       : "&"}});
        astConstVisit(visitor, node);
        format(ctx->state, ", &(StringBuilder){{.sb = &sb}", NULL);
        format(ctx->state, ");\n", NULL);
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

    part = node->stringExpr.parts;
    if (nodeIs(part, StringLit) && part->next == NULL) {
        node->tag = astStringLit;
        node->type = part->type;
        memcpy(&node->_body, &part->_body, CXY_AST_NODE_BODY_SIZE);
    }
    else {
        node->type = makeStringType(ctx->typeTable);
    }
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
