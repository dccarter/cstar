//
// Created by Carter on 2023-04-18.
//

#include "lang/codegen.h"
#include "lang/semantics.h"

#include "lang/ttable.h"

#include "core/alloc.h"

void generateEnumDefinition(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    format(state, "enum {{{>}\n", NULL);
    for (u64 i = 0; i < type->tEnum.count; i++) {
        const EnumOption *option = &type->tEnum.options[i];
        if (i != 0)
            format(state, "\n", NULL);
        writeEnumPrefix(context, type);
        format(state,
               "_{s} = {u64},",
               (FormatArg[]){{.s = option->name}, {.u64 = option->value}});
    }
    format(state, "{<}\n};\n", NULL);

    format(state, "typedef ", NULL);
    writeTypename(context, type->tEnum.base);
    format(state, " ", NULL);
    writeTypename(context, type);
    format(state, ";\n", NULL);

    format(state, "const cxy_enum_names_t ", NULL);
    writeEnumPrefix(context, type);
    format(state, "_enum_names[] = {{{>}\n", NULL);

    for (u64 i = 0; i < type->tEnum.count; i++) {
        const EnumOption *option = &type->tEnum.options[i];
        if (i != 0)
            format(state, "\n", NULL);
        format(state,
               "{{.value = {u64}, .name = \"{s}\"},",
               (FormatArg[]){{.u64 = option->value}, {.s = option->name}});
    }
    format(state, "{<}\n}", NULL);
}

void checkEnumDecl(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    u64 numOptions = countAstNodes(node->enumDecl.options);
    EnumOption *options = mallocOrDie(sizeof(EnumOption) * numOptions);
    AstNode *option = node->enumDecl.options;
    u64 lastValue = 0, i = 0;
    const Type *base = NULL;
    Env env;

    if (node->enumDecl.base)
        base = evalType(visitor, node->enumDecl.base);
    else
        base = getPrimitiveType(ctx->typeTable, prtI64);

    if (!isIntegerType(base)) {
        logError(ctx->L,
                 &node->enumDecl.base->loc,
                 "expecting enum base to be an integral type, got '{t}'",
                 (FormatArg[]){{.t = base}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    defineSymbol(&ctx->env, ctx->L, node->enumDecl.name, node);
    environmentInit(&env);
    environmentAttachUp(&env, &ctx->env);
    pushScope(&env, node);

    for (; option; option = option->next, i++) {
        option->flags |= flgMember;

        if (!defineSymbol(&env, ctx->L, option->enumOption.name, option)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }

        u64 value = option->enumOption.value
                        ? option->enumOption.value->intLiteral.value
                        : lastValue;
        options[i] =
            (EnumOption){.value = value, .name = option->enumOption.name};
        lastValue = value + 1;
        option->enumOption.index = i;
    }

    environmentDetachUp(&env);

    node->type = makeEnum(ctx->typeTable,
                          &(Type){.tag = typEnum,
                                  .name = node->enumDecl.name,
                                  .flags = node->flags,
                                  .tEnum = {.base = base,
                                            .options = options,
                                            .count = numOptions,
                                            .env = &env}});

    free(options);
}
