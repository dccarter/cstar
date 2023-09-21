//
// Created by Carter Mbotho on 2023-08-25.
//
#include "../check.h"
#include "../codegen.h"
#include "../eval.h"

#include "lang/capture.h"
#include "lang/flag.h"
#include "lang/operations.h"
#include "lang/strings.h"
#include "lang/ttable.h"
#include "lang/visitor.h"

#include "core/alloc.h"

static void generateArrayDelete(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    const Type *element = type->array.elementType;
    const Type *unwrapped = unwrapType(element, NULL);
    const Type *stripped = stripAll(element);

    format(state, "\nattr(always_inline)\nstatic void ", NULL);
    writeTypename(context, type);
    format(state, "__builtin_destructor(void *ptr) {{{>}\n", NULL);
    if (isSliceType(type)) {
        writeTypename(context, type);
        format(state, " this = ptr;", NULL);
    }
    else {
        writeTypename(context, element);
        format(state, " *this = ptr;", NULL);
    }

    if (typeIs(unwrapped, Pointer) || typeIs(unwrapped, String) ||
        typeIs(stripped, Struct) || typeIs(stripped, Array) ||
        typeIs(stripped, Tuple)) {
        format(state, "\nfor (u64 i = 0; i < ", NULL);
        if (isSliceType(type))
            format(state, "this->len", NULL);
        else
            format(state, "{u64}", (FormatArg[]){{.u64 = type->array.len}});
        format(state, "; i++) {{{>}\n", NULL);

        if (typeIs(element, Pointer) || typeIs(element, String)) {
            if (isSliceType(type))
                format(state, "CXY__free((void *)this->data[i]);", NULL);
            else
                format(state, "CXY__free((void *)this[i]);", NULL);
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
        format(state, "{<}\n};", NULL);
    }
    format(state, "{<}\n}\n", NULL);
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
               "CXY__builtins_string_builder_append_cstr0(sb->sb, \"null\", "
               "4);\nreturn;{<}\n}\n",
               (FormatArg[]){{.s = target}});

        format(state,
               "CXY__builtins_string_builder_append_char(sb->sb, "
               "'&');",
               NULL);
        deref = pointerLevels(unwrapped);
    }

    switch (stripped->tag) {
    case typNull:
        format(state,
               "CXY__builtins_string_builder_append_cstr0(sb->sb, "
               "\"null\", 4);",
               NULL);
        break;
    case typPrimitive:
        switch (stripped->primitive.id) {
        case prtBool:
            format(state,
                   "CXY__builtins_string_builder_append_bool(sb->sb, "
                   "{cl}this{s}[i]);",
                   (FormatArg[]){{.c = '*'}, {.len = deref}, {.s = target}});
            break;
        case prtChar:
        case prtCChar:
            format(state,
                   "CXY__builtins_string_builder_append_char(sb->sb, "
                   "{cl}this{s}[i]);",
                   (FormatArg[]){{.c = '*'}, {.len = deref}, {.s = target}});
            break;
#define f(I, ...) case prt##I:
            INTEGER_TYPE_LIST(f)
            format(state,
                   "CXY__builtins_string_builder_append_int(sb->sb, "
                   "(i64)({cl}this{s}[i]));",
                   (FormatArg[]){{.c = '*'}, {.len = deref}, {.s = target}});
            break;
#undef f
#define f(I, ...) case prt##I:
            FLOAT_TYPE_LIST(f)
            format(state,
                   "CXY_builtins_string_builder_append_float(sb->sb, "
                   "(f64)({cl}this{s}[i]));",
                   (FormatArg[]){{.c = '*'}, {.len = deref}, {.s = target}});
            break;
#undef f
        default:
            unreachable("UNREACHABLE");
        }
        break;

    case typString:
        format(state,
               "CXY__builtins_string_builder_append_cstr1(sb->sb, "
               "{cl}this{s}[i]);",
               (FormatArg[]){{.c = '*'}, {.len = deref}, {.s = target}});
        break;
    case typEnum:
        format(
            state, "CXY__builtins_string_builder_append_cstr1(sb->sb, ", NULL);
        writeEnumPrefix(context, type);
        format(state,
               "__get_name({cl}this{s}[i]));",
               (FormatArg[]){{.c = '*'}, {.len = deref}, {.s = target}});
        break;
    case typArray:
    case typStruct:
    case typTuple:
        writeTypename(context, stripped);
        format(state,
               "__toString({cl}(&this{s}[i]), sb);",
               (FormatArg[]){{.c = '*'}, {.len = deref}, {.s = target}});
        break;
    case typOpaque:
        format(state,
               "CXY__builtins_string_builder_append_cstr1(sb->sb, "
               "\"<opaque:",
               NULL);
        writeTypename(context, type);
        format(state, ">\");", NULL);
        break;
    case typVoid:
        format(state,
               "CXY__builtins_string_builder_append_cstr0(sb->sb, "
               "\"void\", 4);",
               NULL);
        break;
    case typFunc:
        format(state,
               "CXY__builtins_string_builder_append_cstr0(sb->sb, "
               "\"<func>\", 4);",
               NULL);
        break;
    default:
        unreachable("UNREACHABLE");
    }
}

static void generateArrayToString(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    format(state, "\nstatic void ", NULL);
    writeTypename(context, type);
    format(state, "__toString(", NULL);
    writeTypename(context, type);
    format(state, " this, StringBuilder* sb) {{{>}\n", NULL);

    format(state,
           "CXY__builtins_string_builder_append_char(sb->sb, '[');\n",
           NULL);

    format(state, "for (u64 i = 0; i < ", NULL);
    if (isSliceType(type))
        format(state, "this->len", NULL);
    else
        format(state, "{u64}", (FormatArg[]){{.u64 = type->array.len}});
    format(state, "; i++) {{{>}\n", NULL);

    format(
        state,
        "if (i) {>}\nCXY__builtins_string_builder_append_cstr0(sb->sb, \", \", "
        "2);{<}\n",
        NULL);
    if (isSliceType(type))
        buildStringFormatForIndex(
            context, type->array.elementType, "->data", 0);
    else
        buildStringFormatForIndex(context, type->array.elementType, "", 0);

    format(state, "{<}\n}\n", NULL);

    format(
        state, "CXY__builtins_string_builder_append_char(sb->sb, ']');", NULL);

    format(state, "{<}\n}\n", NULL);
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
    const Type *range_ = unwrapType(range->type, NULL);

    cstring name = makeAnonymousVariable(ctx->strPool, "CXY__for");
    // create an array
    format(ctx->state, "{{{>}\n", NULL);
    if (range->tag == astArrayExpr)
        generateTypeUsage(ctx, range_);
    else
        generateTypeUsage(
            ctx,
            &(const Type){.tag = typPointer,
                          .flags = range_->flags | range->flags,
                          .pointer.pointed = range_->array.elementType});

    format(ctx->state, " _arr_{s} = ", (FormatArg[]){{.s = name}});
    astConstVisit(visitor, range);
    if (isSliceType(range_))
        format(ctx->state, "->data;\n", NULL);
    else
        format(ctx->state, ";\n", NULL);

    // create index variable
    format(ctx->state, "u64 _i_{s} = 0;\n", (FormatArg[]){{.s = name}});

    // Create actual loop variable
    generateTypeUsage(ctx, range_->array.elementType);
    format(ctx->state, " ", NULL);
    astConstVisit(visitor, var->varDecl.names);
    format(ctx->state, " = _arr_{s}[0];\n", (FormatArg[]){{.s = name}});

    format(ctx->state, "for (; _i_{s} < ", (FormatArg[]){{.s = name}});

    if (isSliceType(range_)) {
        astConstVisit(visitor, range);
        format(ctx->state, "->len", NULL);
    }
    else
        format(ctx->state, "{u64}", (FormatArg[]){{.u64 = range_->array.len}});

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
        format(state, "typedef struct _", NULL);
        writeTypename(context, type);
        format(state, " {{{>}\n", NULL);
        format(state, "u64 len;\n", NULL);
        generateTypeUsage(context, type->array.elementType);
        format(state, " *data;{<}\n} *", NULL);
        writeTypename(context, type);
        format(state, ";\n", NULL);
    }
    else {
        format(state, "typedef ", NULL);
        generateTypeUsage(context, type->array.elementType);
        format(state, " ", NULL);
        writeTypename(context, type);
        format(state, "[{u64}]", (FormatArg[]){{.u64 = type->array.len}});
        format(state, ";\n", NULL);
    }

    generateArrayDelete(context, type);
    generateArrayToString(context, type);
}

void checkArrayType(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *element = checkType(visitor, node->arrayType.elementType);

    u64 size = UINT64_MAX;
    if (node->arrayType.dim) {
        // TODO evaluate len
        // evalType(visitor, node->arrayType.dim);
        csAssert0(node->arrayType.dim->tag == astIntegerLit);
        size = node->arrayType.dim->intLiteral.value;
    }

    node->type = makeArrayType(ctx->types, element, size);
}

void checkArrayExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    u64 count = 0;
    const Type *elementType = NULL;
    for (AstNode *elem = node->arrayExpr.elements; elem;
         elem = elem->next, count++) {
        const Type *type = checkType(visitor, elem);
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
            node->type = ERROR_TYPE(ctx);
            return;
        }
    }

    if (elementType == NULL) {
        node->type = makeArrayType(ctx->types, makeAutoType(ctx->types), 0);
    }
    else {
        node->type = makeArrayType(ctx->types, elementType, count);
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
