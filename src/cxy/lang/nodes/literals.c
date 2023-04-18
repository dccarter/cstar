/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-17
 */

#include "lang/codegen.h"
#include "lang/semantics.h"

#include "lang/ttable.h"

void checkLiterals(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    switch (node->tag) {
    case astNullLit:
        node->type = makeNullType(ctx->typeTable);
        break;
    case astBoolLit:
        node->type = getPrimitiveType(ctx->typeTable, prtBool);
        break;
    case astCharLit:
        node->type = getPrimitiveType(ctx->typeTable, prtChar);
        break;
    case astIntegerLit:
        node->type = getPrimitiveType(ctx->typeTable, prtI32);
        break;
    case astFloatLit:
        node->type = getPrimitiveType(ctx->typeTable, prtF32);
        break;
    case astStringLit:
        node->type = makeStringType(ctx->typeTable);
        break;
    default:
        csAssert0("Not a literal");
    }
}

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
               "{s}{u64}",
               (FormatArg[]){{.s = node->intLiteral.hasMinus ? "-" : ""},
                             {.u64 = node->intLiteral.value}});
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