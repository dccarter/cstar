//
// Created by Carter on 2023-08-30.
//

#include "../check.h"
#include "../codegen.h"

#include "lang/flag.h"
#include "lang/operations.h"
#include "lang/strings.h"
#include "lang/ttable.h"

#include <string.h>

void stringBuilderAppend(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);

    const Type *type = unwrapType(node->type, NULL);

    switch (type->tag) {
    case typString:
        if (node->tag == astStringLit) {
            u64 len = strlen(node->stringLiteral.value);
            if (len)
                format(ctx->state,
                       "CXY__builtins_string_builder_append_cstr0(&sb, "
                       "\"{s}\", {u64});\n",
                       (FormatArg[]){{.s = node->stringLiteral.value},
                                     {.u64 = len}});
        }
        else {
            format(ctx->state,
                   "CXY__builtins_string_builder_append_cstr1(&sb, ",
                   NULL);
            astConstVisit(visitor, node);
            format(ctx->state, ");\n", NULL);
        }
        break;
    case typPrimitive:
        switch (type->primitive.id) {
        case prtBool:
            format(ctx->state,
                   "CXY__builtins_string_builder_append_bool(&sb, ",
                   NULL);
            break;
        case prtChar:
            format(ctx->state,
                   "CXY__builtins_string_builder_append_char(&sb, ",
                   NULL);
            break;
#define f(I, ...) case prt##I:
            INTEGER_TYPE_LIST(f)
            format(ctx->state,
                   "CXY__builtins_string_builder_append_int(&sb, (i64)",
                   NULL);
            break;
#undef f
        case prtF32:
        case prtF64:
            format(ctx->state,
                   "CXY__builtins_string_builder_append_float(&sb, ",
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
               "CXY__builtins_string_builder_append_cstr1(&sb, ",
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

void generateStringExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *part = node->stringExpr.parts;

    format(ctx->state,
           "({{{>}\nCXY__builtins_string_builder_t sb = {{};\n"
           "CXY__builtins_string_builder_init(&sb);\n",
           NULL);

    for (; part; part = part->next) {
        // skip empty string literals
        if (nodeIs(part, StringLit) && part->stringLiteral.value[0] == '\0')
            continue;
        stringBuilderAppend(visitor, part);
    }

    format(
        ctx->state, "CXY__builtins_string_builder_release(&sb);{<}\n})", NULL);
}
