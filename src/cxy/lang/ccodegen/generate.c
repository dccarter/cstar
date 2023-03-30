//
// Created by Carter on 2023-03-29.
//
#include "lang/codegen.h"

static void generateCCodeFallback(ConstAstVisitor *visitor, const AstNode *node)
{
    CodeGenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state,
           "/* <unsupported AST tag {u32}> */",
           (FormatArg[]){{.u32 = node->tag}});
}

static void generateProgram(ConstAstVisitor *visitor, const AstNode *node)
{
    CodeGenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state,
           "#include <stdio.h>\n"
           "#include <stdlib.h>\n"
           "#include <stdint.h>\n"
           "\n"
           "\n",
           NULL);
    generateManyAsts(visitor, "\n\n", node->program.decls);
}

static void generateFunc(ConstAstVisitor *visitor, const AstNode *node)
{
    CodeGenContext *ctx = getConstAstVisitorContext(visitor);
}

static void generateVariable(ConstAstVisitor *visitor, const AstNode *node)
{
    CodeGenContext *ctx = getConstAstVisitorContext(visitor);
    if (node->tag == astConstDecl)
        format(ctx->state, "const", NULL);
}

void generateCode(const AstNode *prog)
{
    FormatState state = newFormatState("  ", true);
    CodeGenContext context = {.state = &state};

    // clang-format off
    ConstAstVisitor visitor = makeConstAstVisitor(&context,
    {
        [astProgram] = generateProgram,
        [astFuncDecl] = generateFunc,
        [astVarDecl] = generateVariable,
        [astConstDecl] = generateVariable
    },
    .fallback = generateCCodeFallback);
    // clang-format on

    astConstVisit(&visitor, prog);

    writeFormatState(&state, stdout);
    freeFormatState(&state);
}
