//
// Created by Carter on 2023-03-29.
//
#include "lang/ccodegen/ccodegen.h"
#include "lang/codegen.h"

void generateCCodeFallback(ConstAstVisitor *visitor, const AstNode *node)
{
    CodeGenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state,
           "/* <unsupported AST tag {u32}> */",
           (FormatArg[]){{.u32 = node->tag}});
}

void generateCode(TypeTable *table, const AstNode *prog)
{
    FormatState state = newFormatState("  ", true);
    CCodegenContext context = {.base = {.state = &state}, .table = table};

    cCodegenPrologue(&context, prog);

    writeFormatState(&state, stdout);

    freeFormatState(&state);
}
