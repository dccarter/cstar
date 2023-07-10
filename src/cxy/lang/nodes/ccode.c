/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-21
 */

#include "lang/codegen.h"

void generateCCode(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (node->cCode.kind == cInclude)
        format(ctx->state,
               "#include {s}\n",
               (FormatArg[]){{.s = node->cCode.what->stringLiteral.value}});
    else
        format(ctx->state,
               "#define {s}\n",
               (FormatArg[]){{.s = node->cCode.what->stringLiteral.value}});
}