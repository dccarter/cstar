//
// Created by Carter on 2023-08-30.
//

#include "../check.h"
#include "../codegen.h"
#include "../eval.h"

#include "lang/flag.h"
#include "lang/operations.h"
#include "lang/strings.h"
#include "lang/ttable.h"

#include "core/sb.h"

#include <string.h>

bool evalStringBuilderAppend(EvalContext *ctx, StringBuilder *sb, AstNode *node)
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

void evalStringConcatenation(EvalContext *ctx,
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
    node->stringLiteral.value = makeString(ctx->strings, str);
    free(str);
}

void stringBuilderAppend(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);

    const Type *type = unwrapType(node->type, NULL), *raw = stripAll(type);

    switch (raw->tag) {
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
        switch (raw->primitive.id) {
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
        writeEnumPrefix(ctx, raw);
        format(ctx->state, "__get_name(", NULL);
        astConstVisit(visitor, node);
        format(ctx->state, "));\n", NULL);
        break;

    case typTuple:
    case typArray:
    case typStruct:
        writeTypename(ctx, raw);
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
