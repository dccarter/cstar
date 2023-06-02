//
// Created by Carter on 2023-04-18.
//

#include "lang/codegen.h"
#include "lang/eval.h"
#include "lang/semantics.h"

#include "lang/ttable.h"

static void generateArrayDelete(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    const Type *element = type->array.elementType;
    const Type *unwrapped = unwrapType(element, NULL);
    const Type *stripped = stripAll(element);

    format(state, "attr(always_inline)\nstatic void ", NULL);
    writeTypename(context, type);
    format(state, "__builtin_destructor(void *ptr) {{{>}\n", NULL);
    if (isSliceType(type)) {
        writeTypename(context, type);
        format(state, " this = ptr;\n", NULL);
    }
    else {
        writeTypename(context, element);
        format(state, " *this = ptr;\n", NULL);
    }

    if (typeIs(unwrapped, Pointer) || typeIs(unwrapped, String) ||
        typeIs(stripped, Struct) || typeIs(stripped, Array) ||
        typeIs(stripped, Tuple)) {
        format(state, "for (u64 i = 0; i < ", NULL);
        if (isSliceType(type))
            format(state, "this->len", NULL);
        else
            format(state, "{u64}", (FormatArg[]){{.u64 = type->array.len}});
        format(state, "; i++) {{{>}\n", NULL);

        if (typeIs(element, Pointer) || typeIs(element, String)) {
            if (isSliceType(type))
                format(state, "cxy_free((void *)this->data[i]);", NULL);
            else
                format(state, "cxy_free((void *)this[i]);", NULL);
        }
        else if (typeIs(stripped, Struct) || typeIs(stripped, Array) ||
                 typeIs(stripped, Tuple)) {
            if (isSliceType(type)) {
                writeTypename(context, stripped);
                format(state, "__builtin_destructor(&this->data[i]);", NULL);
            }
            else {
                writeTypename(context, stripped);
                format(state, "__builtin_destructor(&this[i]);", NULL);
            }
        }
        format(state, "{<}\n};\n", NULL);
    }
    format(state, "{<}\n}", NULL);
}

static void buildStringFormatForIndex(CodegenContext *context,
                                      const Type *type,
                                      cstring target,
                                      u64 deref)
{
    FormatState *state = context->state;
    const Type *unwrapped = unwrapType(type, NULL);
    const Type *stripped = stripAll(type);

    if (typeIs(unwrapped, Pointer)) {
        format(state,
               "if (this{s}[i] != NULL) {{{>}\n"
               "__cxy_builtins_string_builder_append_cstr0(sb->sb, \"null\", "
               "4);\nreturn;{<}\n}\n",
               (FormatArg[]){{.s = target}});

        format(state,
               "__cxy_builtins_string_builder_append_char(sb->sb, "
               "'&');\n",
               NULL);
        deref = pointerLevels(unwrapped);
    }

    switch (stripped->tag) {
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
                   "{cl}this{s}[i]);\n",
                   (FormatArg[]){{.c = '*'}, {.len = deref}, {.s = target}});
            break;
        case prtChar:
            format(state,
                   "__cxy_builtins_string_builder_append_char(sb->sb, "
                   "{cl}this{s}[i]);\n",
                   (FormatArg[]){{.c = '*'}, {.len = deref}, {.s = target}});
            break;
#define f(I, ...) case prt##I:
            INTEGER_TYPE_LIST(f)
            format(state,
                   "__cxy_builtins_string_builder_append_int(sb->sb, "
                   "(i64)({cl}this{s}[i]));\n",
                   (FormatArg[]){{.c = '*'}, {.len = deref}, {.s = target}});
            break;
#undef f
#define f(I, ...) case prt##I:
            FLOAT_TYPE_LIST(f)
            format(state,
                   "__cxy_builtins_string_builder_append_float(sb->sb, "
                   "(f64)({cl}this{s}[i]));\n",
                   (FormatArg[]){{.c = '*'}, {.len = deref}, {.s = target}});
            break;
#undef f
        default:
            unreachable("UNREACHABLE");
        }
        break;

    case typString:
        format(state,
               "__cxy_builtins_string_builder_append_cstr1(sb->sb, "
               "{cl}this{s}[i]);\n",
               (FormatArg[]){{.c = '*'}, {.len = deref}, {.s = target}});
        break;
    case typEnum:
        format(
            state, "__cxy_builtins_string_builder_append_cstr1(sb->sb, ", NULL);
        writeEnumPrefix(context, type);
        format(state,
               "__get_name({cl}this{s}[i]));\n",
               (FormatArg[]){{.c = '*'}, {.len = deref}, {.s = target}});
        break;
    case typArray:
    case typStruct:
    case typTuple:
        writeTypename(context, stripped);
        format(state,
               "__toString({cl}(&this{s}[i]), sb);\n",
               (FormatArg[]){{.c = '*'}, {.len = deref}, {.s = target}});
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

static void generateArrayToString(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    format(state, "static void ", NULL);
    writeTypename(context, type);
    format(state, "__toString(", NULL);
    writeTypename(context, type);
    if (isSliceType(type))
        format(state, " this, StringBuilder* sb) {{{>}\n", NULL);
    else
        format(state, " *this, StringBuilder* sb) {{{>}\n", NULL);

    format(
        state, "__cxy_builtins_string_builder_append_char(sb->sb, '[');", NULL);

    format(state, "for (u64 i = 0; i < ", NULL);
    if (isSliceType(type))
        format(state, "this->len", NULL);
    else
        format(state, "{u64}", (FormatArg[]){{.u64 = type->array.len}});
    format(state, "; i++) {{{>}\n", NULL);

    format(state,
           "if (i) __cxy_builtins_string_builder_append_cstr0(sb->sb, \", \", "
           "2);\n",
           NULL);
    if (isSliceType(type))
        buildStringFormatForIndex(
            context, type->array.elementType, "->data", 0);
    else
        buildStringFormatForIndex(context, type->array.elementType, "", 0);

    format(state, "{<}\n}\n", NULL);

    format(
        state, "__cxy_builtins_string_builder_append_char(sb->sb, ']');", NULL);

    format(state, "{<}\n}", NULL);
}

void generateArrayToSlice(ConstAstVisitor *visitor,
                          const Type *slice,
                          const AstNode *value)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);

    format(ctx->state, "&(__", NULL);
    writeTypename(ctx, slice);
    format(ctx->state, "){{.data = ", NULL);
    format(ctx->state, "(", NULL);
    writeTypename(ctx, slice->array.elementType);
    format(ctx->state, "*)", NULL);
    if (nodeIs(value, ArrayExpr)) {
        format(ctx->state, "(", NULL);
        writeTypename(ctx, value->type);
        format(ctx->state, ")", NULL);
    }
    astConstVisit(visitor, value);
    format(ctx->state,
           ", .len = {u64}}",
           (FormatArg[]){{.u64 = value->type->array.len}});
}

void generateForStmtArray(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *var = node->forStmt.var;
    const AstNode *range = node->forStmt.range;

    cstring name = makeAnonymousVariable(ctx->strPool, "cyx_for");
    // create an array
    format(ctx->state, "{{{>}\n", NULL);
    if (range->tag == astArrayExpr)
        generateTypeUsage(ctx, range->type);
    else
        generateTypeUsage(
            ctx,
            &(const Type){.tag = typPointer,
                          .flags =
                              range->type->flags | node->forStmt.range->flags,
                          .pointer.pointed = range->type->array.elementType});

    format(ctx->state, " _arr_{s} = ", (FormatArg[]){{.s = name}});
    astConstVisit(visitor, range);
    if (isSliceType(range->type))
        format(ctx->state, "->data;\n", NULL);
    else
        format(ctx->state, ";\n", NULL);

    // create index variable
    format(ctx->state, "u64 _i_{s} = 0;\n", (FormatArg[]){{.s = name}});

    // Create actual loop variable
    generateTypeUsage(ctx, range->type->array.elementType);
    format(ctx->state, " ", NULL);
    astConstVisit(visitor, var->varDecl.names);
    format(ctx->state, " = _arr_{s}[0];\n", (FormatArg[]){{.s = name}});

    format(ctx->state, "for (; _i_{s} < ", (FormatArg[]){{.s = name}});

    if (isSliceType(range->type)) {
        astConstVisit(visitor, range);
        format(ctx->state, "->len", NULL);
    }
    else
        format(ctx->state,
               "{u64}",
               (FormatArg[]){{.u64 = range->type->array.len}});

    format(ctx->state, "; _i_{s}++, ", (FormatArg[]){{.s = name}});

    astConstVisit(visitor, var->varDecl.names);
    format(ctx->state,
           " = _arr_{s}[_i_{s}]",
           (FormatArg[]){{.s = name}, {.s = name}});

    format(ctx->state, ") ", NULL);
    astConstVisit(visitor, node->forStmt.body);

    format(ctx->state, "{<}\n}", NULL);
}

void generateArrayExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    generateManyAstsWithDelim(
        visitor, "{{", ", ", "}", node->arrayExpr.elements);
}

void generateArrayDeclaration(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    if (isSliceType(type)) {
        format(state, "typedef struct ", NULL);
        writeTypename(context, type);
        format(state, " {{{>}\n", NULL);
        format(state, "u64 len;\n", NULL);
        generateTypeUsage(context, type->array.elementType);
        format(state, " *data;{<}\n} __", NULL);
        writeTypename(context, type);
        format(state, ";\ntypedef __", NULL);
        writeTypename(context, type);
        format(state, " *", NULL);
        writeTypename(context, type);
    }
    else {
        format(state, "typedef ", NULL);
        generateTypeUsage(context, type->array.elementType);
        format(state, " ", NULL);
        writeTypename(context, type);
        format(state, "[{u64}]", (FormatArg[]){{.u64 = type->array.len}});
    }

    format(state, ";\n", NULL);
    generateArrayDelete(context, type);
    format(state, "\n", NULL);
    generateArrayToString(context, type);
}

void checkArrayType(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    if (node->type)
        return;

    const Type *element = evalType(visitor, node->arrayType.elementType);

    u64 size = UINT64_MAX;
    if (node->arrayType.dim) {
        // TODO evaluate len
        evalType(visitor, node->arrayType.dim);
        csAssert0(node->arrayType.dim->tag == astIntegerLit);
        size = node->arrayType.dim->intLiteral.value;
    }

    node->type = makeArrayType(ctx->typeTable, element, size);
}

void checkArrayExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    u64 count = 0;
    const Type *elementType = NULL;
    for (AstNode *elem = node->arrayExpr.elements; elem;
         elem = elem->next, count++) {
        const Type *type = evalType(visitor, elem);
        if (elementType == NULL) {
            elementType = type;
            continue;
        }

        if (!isTypeAssignableFrom(elementType, type)) {
            logError(ctx->L,
                     &elem->loc,
                     "inconsistent array types in array, expecting '{t}', "
                     "got '{t}'",
                     (FormatArg[]){{.t = elementType}, {.t = type}});
        }
    }
    if (elementType == NULL) {
        node->type =
            makeArrayType(ctx->typeTable, makeAutoType(ctx->typeTable), 0);
    }
    else {
        node->type = makeArrayType(ctx->typeTable, elementType, count);
    }
}

void evalArrayExpr(AstVisitor *visitor, AstNode *node)
{
    AstNode *arg = node->arrayExpr.elements;
    for (; arg; arg = arg->next) {
        if (!evaluate(visitor, arg)) {
            node->tag = astError;
            return;
        }
    }
}
