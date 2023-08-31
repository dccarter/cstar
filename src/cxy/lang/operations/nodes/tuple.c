//
// Created by Carter on 2023-08-30.
//

#include "../check.h"
#include "../codegen.h"

#include "lang/flag.h"
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
                   "CXY_free((void *)this->_{u64});",
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

    if (typeIs(unwrapped, Pointer)) {
        format(state,
               "if (this->_{u64} != NULL) {{{>}\n"
               "CXY__builtins_string_builder_append_cstr0(sb->sb, \"null\", "
               "4);\nreturn;{<}\n}\n",
               (FormatArg[]){{.u64 = index}});

        format(state,
               "CXY__builtins_string_builder_append_char(sb->sb, "
               "'&');\n",
               NULL);
        deref = pointerLevels(unwrapped);
    }

    switch (stripped->tag) {
    case typNull:
        format(state,
               "CXY__builtins_string_builder_append_cstr0(sb->sb, "
               "\"null\", 4);\n",
               NULL);
        break;
    case typPrimitive:
        switch (stripped->primitive.id) {
        case prtBool:
            format(state,
                   "CXY__builtins_string_builder_append_bool(sb->sb, "
                   "{cl}this->_{u64});\n",
                   (FormatArg[]){{.c = '*'}, {.len = deref}, {.u64 = index}});
            break;
        case prtChar:
            format(state,
                   "CXY__builtins_string_builder_append_char(sb->sb, "
                   "{cl}this->_{u64});\n",
                   (FormatArg[]){{.c = '*'}, {.len = deref}, {.u64 = index}});
            break;
#define f(I, ...) case prt##I:
            INTEGER_TYPE_LIST(f)
            format(state,
                   "CXY__builtins_string_builder_append_int(sb->sb, "
                   "(i64)({cl}this->_{u64}));\n",
                   (FormatArg[]){{.c = '*'}, {.len = deref}, {.u64 = index}});
            break;
#undef f
#define f(I, ...) case prt##I:
            FLOAT_TYPE_LIST(f)
            format(state,
                   "CXY__builtins_string_builder_append_float(sb->sb, "
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
               "CXY__builtins_string_builder_append_cstr1(sb->sb, "
               "{cl}this->_{u64});\n",
               (FormatArg[]){{.c = '*'}, {.len = deref}, {.u64 = index}});
        break;
    case typEnum:
        format(
            state, "CXY__builtins_string_builder_append_cstr1(sb->sb, ", NULL);
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
    case typOpaque:
        format(state,
               "CXY__builtins_string_builder_append_cstr1(sb->sb, "
               "\"<opaque:",
               NULL);
        writeTypename(context, type);
        format(state, ">\");\n", NULL);
        break;
    case typVoid:
        format(state,
               "CXY__builtins_string_builder_append_cstr0(sb->sb, "
               "\"void\", 4);\n",
               NULL);
        break;
    case typFunc:
        format(state,
               "CXY__builtins_string_builder_append_cstr0(sb->sb, "
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
        state, "CXY__builtins_string_builder_append_char(sb->sb, '(');", NULL);

    u64 y = 0;
    for (u64 i = 0; i < type->tuple.count; i++) {
        const Type *member = type->tuple.members[i];
        if (y++ != 0)
            format(state,
                   "CXY__builtins_string_builder_append_cstr0(sb->sb, "
                   "\", \", 2);\n",
                   NULL);

        buildStringFormatForMember(context, member, i, 0);
    }

    format(
        state, "CXY__builtins_string_builder_append_char(sb->sb, ')');", NULL);

    format(state, "{<}\n}", NULL);
}

void generateTupleDefinition(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;

    format(state, "typedef struct {{{>}\n", NULL);
    format(state, "void *__mm;\n", NULL);
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
    const AstNode *arg = node->tupleExpr.elements;

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
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *elements = node->tupleExpr.elements, *element = elements;
    node->tupleExpr.len = node->tupleExpr.len ?: countAstNodes(elements);
    const Type **elements_ = mallocOrDie(sizeof(Type *) * node->tupleExpr.len);

    for (u64 i = 0; element; element = element->next, i++) {
        elements_[i] = checkType(visitor, element);
        if (typeIs(elements_[i], Error))
            node->type = element->type;
    }

    if (!typeIs(node->type, Error)) {
        node->type = makeTupleType(
            ctx->types, elements_, node->tupleExpr.len, node->flags & flgConst);
    }

    free(elements_);
}

void checkTupleType(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *elems = node->tupleType.elements, *elem = elems;
    u64 count = node->tupleType.len ?: countAstNodes(node->tupleType.elements);
    const Type **elems_ = mallocOrDie(sizeof(Type *) * count);

    for (u64 i = 0; elem; elem = elem->next, i++) {
        elems_[i] = checkType(visitor, elem);
        if (typeIs(elems_[i], Error))
            node->type = ERROR_TYPE(ctx);
    }

    if (typeIs(node->type, Error)) {
        free(elems_);
        return;
    }

    node->type =
        makeTupleType(ctx->types, elems_, count, node->flags & flgConst);

    free(elems_);
}
