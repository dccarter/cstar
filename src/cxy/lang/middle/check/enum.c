//
// Created by Carter Mbotho on 2024-01-09.
//

#include "check.h"

#include "../eval/eval.h"

#include "core/alloc.h"


static int compareEnumOptionsByValue(const void *lhs, const void *rhs)
{
    return (int)(((EnumOption *)lhs)->value - ((EnumOption *)rhs)->value);
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

    if (!evaluate(ctx->evaluator, node)) {
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
                                            .optionsCount = numOptions,
                                            .decl = node}});

    free(options);
}
