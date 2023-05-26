//
// Created by Carter on 2023-04-18.
//

#include "lang/codegen.h"
#include "lang/eval.h"
#include "lang/semantics.h"

#include "lang/ttable.h"

#include "core/alloc.h"

static void generateTupleDelete(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    format(state, "attr(always_inline)\nstatic void ", NULL);
    writeTypename(context, type);
    format(state, "__builtin_destructor(void *ptr) {{{>}\n", NULL);
    writeTypename(context, type);
    format(state, " *this = ptr;\n", NULL);

    u64 y = 0;
    for (u64 i = 0; i < type->tuple.count; i++) {
        const Type *member = type->tuple.members[i];
        const Type *stripped = stripAll(member);
        const Type *unwrapped = unwrapType(member, NULL);

        if (y++ != 0)
            format(state, "\n", NULL);

        if (typeIs(unwrapped, Pointer) || typeIs(unwrapped, String)) {
            format(state,
                   "cxy_free((void *)this->_{u64});",
                   (FormatArg[]){{.u64 = i}});
        }
        else if (typeIs(stripped, Struct) || typeIs(stripped, Array) ||
                 typeIs(stripped, Tuple)) {
            writeTypename(context, stripped);
            format(state,
                   "__builtin_destructor(&this->_{u64});",
                   (FormatArg[]){{.u64 = i}});
        }
    }

    format(state, "{<}\n}", NULL);
}

static void buildStringFormatForMember(CodegenContext *context,
                                       const Type *type,
                                       u64 index,
                                       u64 deref)
{
    FormatState *state = context->state;
    const Type *unwrapped = unwrapType(type, NULL);
    const Type *stripped = stripAll(type);

    switch (unwrapped->tag) {
    case typNull:
        format(state,
               "__cxy_builtins_string_builder_append_cstr0(sb->sb, "
               "\"null\", 4);\n",
               NULL);
        break;
    case typPrimitive:
        switch (stripped->primitive.id) {
        case prtBool:
            format(state,
                   "__cxy_builtins_string_builder_append_bool(sb->sb, "
                   "{cl}this->_{u64});\n",
                   (FormatArg[]){{.c = '*'}, {.len = deref}, {.u64 = index}});
            break;
        case prtChar:
            format(state,
                   "__cxy_builtins_string_builder_append_char(sb->sb, "
                   "{cl}this->_{u64});\n",
                   (FormatArg[]){{.c = '*'}, {.len = deref}, {.u64 = index}});
            break;
#define f(I, ...) case prt##I:
            INTEGER_TYPE_LIST(f)
            format(state,
                   "__cxy_builtins_string_builder_append_int(sb->sb, "
                   "(i64)({cl}this->_{u64}));\n",
                   (FormatArg[]){{.c = '*'}, {.len = deref}, {.u64 = index}});
            break;
#undef f
#define f(I, ...) case prt##I:
            FLOAT_TYPE_LIST(f)
            format(state,
                   "__cxy_builtins_string_builder_append_float(sb->sb, "
                   "(f64)({cl}this->_{u64}));\n",
                   (FormatArg[]){{.c = '*'}, {.len = deref}, {.u64 = index}});
            break;
#undef f
        default:
            unreachable("UNREACHABLE");
        }
        break;

    case typString:
        format(state,
               "__cxy_builtins_string_builder_append_cstr1(sb->sb, "
               "{cl}this->_{u64});\n",
               (FormatArg[]){{.c = '*'}, {.len = deref}, {.u64 = index}});
        break;
    case typEnum:
        format(
            state, "__cxy_builtins_string_builder_append_cstr1(sb->sb, ", NULL);
        writeEnumPrefix(context, type);
        format(state,
               "__get_name({cl}this->_{u64}));\n",
               (FormatArg[]){{.c = '*'}, {.len = deref}, {.u64 = index}});
        break;
    case typArray:
    case typStruct:
    case typTuple:
        writeTypename(context, stripped);
        format(state,
               "__toString({cl}this->_{u64}, sb);\n",
               (FormatArg[]){{.c = deref ? '*' : '&'},
                             {.len = deref ? deref - 1 : 1},
                             {.u64 = index}});
        break;
    case typPointer:
        format(state,
               "__cxy_builtins_string_builder_append_char(sb->sb, "
               "'&');\n",
               NULL);
        buildStringFormatForMember(
            context, stripped, index, pointerLevels(unwrapped));
        break;
    case typOpaque:
        format(state,
               "__cxy_builtins_string_builder_append_cstr1(sb->sb, "
               "\"<opaque:",
               NULL);
        writeTypename(context, type);
        format(state, ">\");\n", NULL);
        break;
    case typVoid:
        format(state,
               "__cxy_builtins_string_builder_append_cstr0(sb->sb, "
               "\"void\", 4);\n",
               NULL);
        break;
    case typFunc:
        format(state,
               "__cxy_builtins_string_builder_append_cstr0(sb->sb, "
               "\"<func>\", 4);\n",
               NULL);
        break;
    default:
        unreachable("UNREACHABLE");
    }
}

static void generateTupleToString(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    format(state, "static void ", NULL);
    writeTypename(context, type);
    format(state, "__toString(", NULL);
    writeTypename(context, type);
    format(state, " *this, StringBuilder* sb) {{{>}\n", NULL);
    format(
        state, "__cxy_builtins_string_builder_append_char(sb->sb, '(');", NULL);

    u64 y = 0;
    for (u64 i = 0; i < type->tuple.count; i++) {
        const Type *member = type->tuple.members[i];
        if (y++ != 0)
            format(state,
                   "__cxy_builtins_string_builder_append_cstr0(sb->sb, "
                   "\", \", 2);\n",
                   NULL);

        buildStringFormatForMember(context, member, i, 0);
    }

    format(
        state, "__cxy_builtins_string_builder_append_char(sb->sb, ')');", NULL);

    format(state, "{<}\n}", NULL);
}

void generateTupleDefinition(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;

    format(state, "typedef struct {{{>}\n", NULL);
    format(state, "void *__mgmt;\n", NULL);
    for (u64 i = 0; i < type->tuple.count; i++) {
        if (i != 0)
            format(state, "\n", NULL);
        generateTypeUsage(context, type->tuple.members[i]);
        format(state, " _{u64};", (FormatArg[]){{.u64 = i}});
    }
    format(state, "{<}\n} ", NULL);
    writeTypename(context, type);

    format(state, ";\n", NULL);
    generateTupleDelete(context, type);
    format(state, "\n", NULL);
    generateTupleToString(context, type);
}

void generateTupleExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *arg = node->tupleExpr.args;

    format(ctx->state, "(", NULL);
    generateTypeUsage(ctx, node->type);
    format(ctx->state, ")", NULL);

    format(ctx->state, "{{", NULL);
    for (u64 i = 0; arg; arg = arg->next, i++) {
        if (i != 0)
            format(ctx->state, ", ", NULL);
        format(ctx->state, "._{u64} = ", (FormatArg[]){{.u64 = i}});
        astConstVisit(visitor, arg);
    }

    format(ctx->state, "}", NULL);
}

void checkTupleExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    u64 count = countAstNodes(node->tupleExpr.args);
    const Type **args = mallocOrDie(sizeof(Type *) * count);
    AstNode *arg = node->tupleExpr.args;

    for (u64 i = 0; arg; arg = arg->next, i++) {
        args[i] = evalType(visitor, arg);
        if (args[i] == ERROR_TYPE(ctx))
            node->type = ERROR_TYPE(ctx);
    }

    if (node->type == NULL) {
        node->type = makeTupleType(ctx->typeTable, args, count, flgNone);
    }

    free(args);
}

void checkTupleType(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    u64 count = countAstNodes(node->tupleType.args);
    const Type **args = mallocOrDie(sizeof(Type *) * count);

    AstNode *arg = node->tupleType.args;
    for (u64 i = 0; arg; arg = arg->next, i++) {
        args[i] = evalType(visitor, arg);
        if (args[i] == ERROR_TYPE(ctx))
            node->type = ERROR_TYPE(ctx);
    }

    if (node->type == NULL)
        node->type = makeTupleType(ctx->typeTable, args, count, flgNone);

    free(args);
}

void evalTupleExpr(AstVisitor *visitor, AstNode *node)
{
    u64 i = 0;
    AstNode *arg = node->tupleExpr.args;
    for (; arg; arg = arg->next, i++) {
        if (!evaluate(visitor, arg)) {
            node->tag = astError;
            return;
        }
    }
    node->tupleExpr.len = i;
}
