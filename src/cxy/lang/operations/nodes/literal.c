//
// Created by Carter on 2023-08-30.
//

#include "../check.h"
#include "../codegen.h"

#include "lang/operations.h"

#include "lang/flag.h"
#include "lang/strings.h"
#include "lang/ttable.h"
#include "lang/visitor.h"

#include <memory.h>

void generateLiteral(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);

    switch (node->tag) {
    case astNullLit:
        format(ctx->state, "nullptr", NULL);
        break;
    case astBoolLit:
        format(
            ctx->state,
            "{s}",
            (FormatArg[]){{.s = node->boolLiteral.value ? "true" : "false"}});
        break;
    case astCharLit:
        format(ctx->state,
               "{u32}",
               (FormatArg[]){{.u32 = node->charLiteral.value}});
        break;
    case astIntegerLit:

        format(ctx->state,
               "{i64}",
               (FormatArg[]){{.i64 = integerLiteralValue(node)}});
        break;
    case astFloatLit:
        format(ctx->state,
               "{f64}",
               (FormatArg[]){{.f64 = node->floatLiteral.value}});
        break;
    case astStringLit:
        format(ctx->state,
               "\"{s}\"",
               (FormatArg[]){{.s = node->stringLiteral.value}});
        break;
    default:
        break;
    }
}

void checkLiteral(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    switch (node->tag) {
    case astNullLit:
        node->type = makeNullType(ctx->types);
        break;
    case astBoolLit:
        node->type = getPrimitiveType(ctx->types, prtBool);
        break;
    case astCharLit:
        node->type = getPrimitiveType(ctx->types, prtChar);
        break;
    case astIntegerLit:
        node->type =
            getIntegerTypeForLiteral(ctx->types, integerLiteralValue(node));
        break;
    case astFloatLit:
        node->type = getPrimitiveType(ctx->types, prtF64);
        break;
    case astStringLit:
        node->type = makeStringType(ctx->types);
        break;
    default:
        unreachable("NOT LITERAL");
    }
}
