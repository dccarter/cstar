//
// Created by Carter on 2023-08-30.
//

#include "../check.h"
#include "../codegen.h"

#include "lang/flag.h"
#include "lang/ttable.h"

#include "core/alloc.h"

static int compareEnumOptionsByValue(const void *lhs, const void *rhs)
{
    return (int)(((EnumOption *)lhs)->value - ((EnumOption *)rhs)->value);
}

void generateEnumDefinition(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    format(state, "typedef ", NULL);
    writeTypename(context, type->tEnum.base);
    format(state, " ", NULL);
    writeTypename(context, type);
    format(state, ";\n", NULL);

    format(state, "enum {{{>}\n", NULL);
    for (u64 i = 0; i < type->tEnum.optionsCount; i++) {
        const EnumOption *option = &type->tEnum.options[i];
        if (i != 0)
            format(state, "\n", NULL);
        writeEnumPrefix(context, type);
        format(state,
               "_{s} = {u64},",
               (FormatArg[]){{.s = option->name}, {.u64 = option->value}});
    }
    format(state, "{<}\n};\n", NULL);

    format(state, "\nconst char *", NULL);
    writeEnumPrefix(context, type);
    format(state, "__get_name(", NULL);
    writeEnumPrefix(context, type);
    format(state, " value) {{{>}\n", NULL);
    format(state, "switch((i64)value) {{{>}\n", NULL);
    u64 prev = 0;
    for (u64 i = 0; i < type->tEnum.optionsCount; i++) {
        const EnumOption *option = &type->tEnum.options[i];
        if (i && option->value == prev)
            continue;
        format(state, "case ", NULL);
        writeEnumPrefix(context, type);
        format(state,
               "_{s}: return \"{s}.{s}\";\n",
               (FormatArg[]){{.s = option->name},
                             {.s = type->name ?: ""},
                             {.s = option->name}});
        prev = option->value;
    }
    format(state, "default: return \"(unknown)\";", NULL);
    format(state, "{<}\n}{<}\n}\n", NULL);
}

void checkEnumDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);

    u64 numOptions = node->enumDecl.len;
    AstNode *option = node->enumDecl.options;
    const Type *base = NULL;

    if (node->enumDecl.base)
        base = checkType(visitor, node->enumDecl.base);
    else
        base = getPrimitiveType(ctx->types, prtI64);

    if (!isIntegerType(base)) {
        logError(ctx->L,
                 &node->enumDecl.base->loc,
                 "expecting enum base to be an integral type, got '{t}'",
                 (FormatArg[]){{.t = base}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    EnumOption *options = mallocOrDie(sizeof(EnumOption) * numOptions);
    for (u64 i = 0; option; option = option->next, i++) {
        options[i] =
            (EnumOption){.value = option->enumOption.value->intLiteral.value,
                         .name = option->enumOption.name};
    }

    qsort(options, numOptions, sizeof(EnumOption), compareEnumOptionsByValue);

    node->type = makeEnum(ctx->types,
                          &(Type){.tag = typEnum,
                                  .name = node->enumDecl.name,
                                  .flags = node->flags,
                                  .tEnum = {.base = base,
                                            .options = options,
                                            .optionsCount = numOptions}});

    free(options);
}