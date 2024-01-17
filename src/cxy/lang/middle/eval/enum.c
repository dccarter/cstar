//
// Created by Carter Mbotho on 2024-01-09.
//

#include "eval.h"

#include "lang/frontend/flag.h"

static bool evalEnumOption(AstVisitor *visitor, AstNode *node)
{
    AstNode *value = node->enumOption.value;
    EvalContext *ctx = getAstVisitorContext(visitor);

    if (value == NULL || isIntegralLiteral(value))
        return true;

    FileLoc loc = value->loc;
    if (evaluate(visitor, value)) {
        if (isIntegralLiteral(value))
            return true;

        logError(
            ctx->L,
            &loc,
            "enum value should evaluate to a compile time integer constant",
            NULL);
    }

    node->tag = astError;
    return false;
}

void evalEnumDecl(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    i64 value = 0, len = 0;
    AstNode *option = node->enumDecl.options;
    for (; option; option = option->next, len++) {
        if (!evalEnumOption(visitor, option)) {
            node->tag = astError;
            return;
        }

        if (option->enumOption.value == NULL) {
            option->enumOption.value =
                makeIntegerLiteral(ctx->pool, &option->loc, value, NULL, NULL);
        }
        else {
            value = integerLiteralValue(option->enumOption.value) + 1;
        }
    }

    node->enumDecl.len = len;
    node->flags |= flgVisited;
}
