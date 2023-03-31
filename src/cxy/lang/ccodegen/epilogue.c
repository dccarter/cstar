//
// Created by Carter on 2023-03-29.
//

#include "ccodegen.h"
#include "lang/ttable.h"

#include "core/alloc.h"

#include <string.h>

#define CXY_EPILOGUE_SRC_FILE CXY_SOURCE_LANG_DIR "/ccodegen/epilogue.cxy.c"

static void programEpilogue(ConstAstVisitor *visitor, const AstNode *node)
{
    CCodegenContext *ctx = getConstAstVisitorContext(visitor);
    size_t bytes = 0;
    format(ctx->base.state,
           "\n"
           "/* --------------------- Generated EPILOGUE --------------*/\n"
           "\n",
           NULL);
    generateManyAsts(visitor, "\n\n", node->program.decls);

    format(ctx->base.state,
           "\n"
           "/* --------------------- epilogue.cxy.c --------------*/\n"
           "\n",
           NULL);

    append(ctx->base.state, readFile(CXY_EPILOGUE_SRC_FILE, &bytes), bytes);
    format(ctx->base.state, "\n", NULL);
}

static void generatePathElement(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "{s}", (FormatArg[]){{.s = node->pathElement.name}});
}

static void generatePath(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    generateManyAstsWithDelim(visitor, "", ".", "", node->path.elements);
}

static void generateIdentifier(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "{s}", (FormatArg[]){{.s = node->ident.value}});
}

static void generateFuncParam(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    generateTypeUsage((CCodegenContext *)ctx, node->type);
    format(ctx->state, " {s}", (FormatArg[]){{.s = node->funcParam.name}});
}

static void generateFunc(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    TypeTable *table = ((CCodegenContext *)ctx)->table;

    if (node->flags & flgMain) {
        if (isIntegerType(table, node->type->func.retType)) {
            format(ctx->state,
                   "#define __CXY_MAIN_INVOKE(...) return "
                   "__cxy_main(__VA_ARGS__)\n\n",
                   NULL);
        }
        else {
            format(ctx->state,
                   "#define __CXY_MAIN_INVOKE(...) __cxy_main(__VA_ARGS__); "
                   "return EXIT_SUCCESS\n\n",
                   NULL);
        }
    }

    if (node->flags & flgNative)
        format(ctx->state, "extern ", NULL);

    generateTypeUsage((CCodegenContext *)ctx, node->type->func.retType);
    if (node->flags & flgMain)
        format(ctx->state, " __cxy_main", NULL);
    else
        format(ctx->state, " {s}", (FormatArg[]){{.s = node->funcDecl.name}});

    generateManyAstsWithDelim(visitor, "(", ", ", ")", node->funcDecl.params);

    if (node->flags & flgNative) {
        format(ctx->state, ";", NULL);
    }
    else {
        format(ctx->state, " ", NULL);
        if (node->funcDecl.body->tag == astBlockStmt) {
            astConstVisit(visitor, node->funcDecl.body);
        }
        else {
            format(ctx->state, "{{{>}\n", NULL);
            if (node->type->func.retType != makeVoidType(table)) {
                format(ctx->state, "return ", NULL);
            }
            astConstVisit(visitor, node->funcDecl.body);
            format(ctx->state, "{<}\n}", NULL);
        }
    }
}

static void generateTypeDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    CCodegenContext *ctx = getConstAstVisitorContext(visitor);
    generateTypeUsage(ctx, node->type);
}

static void generateVariable(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);

    if (node->tag == astConstDecl)
        format(ctx->state, "const ", NULL);
    generateTypeUsage((CCodegenContext *)ctx, node->type);

    format(ctx->state, " ", NULL);
    astConstVisit(visitor, node->varDecl.names);

    if (node->varDecl.init) {
        format(ctx->state, " = ", NULL);
        astConstVisit(visitor, node->varDecl.init);
    }
    format(ctx->state, ";", NULL);
}

static void generateLiteral(ConstAstVisitor *visitor, const AstNode *node)
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

static void generateAddressOf(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "&", NULL);
    astConstVisit(visitor, node->unaryExpr.operand);
}

static void generateStatementExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    astConstVisit(visitor, node->stmtExpr.stmt);
}

static void generateTupleExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *arg = node->tupleExpr.args;

    format(ctx->state, "(", NULL);
    generateTypeUsage((CCodegenContext *)ctx, node->type);
    format(ctx->state, ")", NULL);

    format(ctx->state, "{{", NULL);
    for (u64 i = 0; arg; arg = arg->next, i++) {
        if (i != 0)
            format(ctx->state, ", ", NULL);
        format(ctx->state, "._{u64} = ", (FormatArg[]){{.u64 = i}});
        astConstVisit(visitor, arg);
    }

    format(ctx->state, "}", NULL);
}

static void generateMemberExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *target = node->memberExpr.target,
                  *member = node->memberExpr.member;

    astConstVisit(visitor, target);
    if (target->type->tag == typPointer)
        format(ctx->state, "->", NULL);
    else
        format(ctx->state, ".", NULL);
    if (member->tag == astIntegerLit) {
        format(ctx->state,
               "_{u64}",
               (FormatArg[]){{.u64 = member->intLiteral.value}});
    }
    else {
        format(ctx->state, "{s}", (FormatArg[]){{.s = member->ident.value}});
    }
}

static void generateCallExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->callExpr.callee);
    generateManyAstsWithDelim(visitor, "(", ", ", ")", node->callExpr.args);
}

static void generateBlock(ConstAstVisitor *visitor, const AstNode *node)
{
    generateManyAstsWithinBlock(visitor, "\n", node->blockStmt.stmts, true);
}

static void generateExpressionStmt(ConstAstVisitor *visitor,
                                   const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->exprStmt.expr);
    format(ctx->state, ";", NULL);
}

static void generateReturn(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "return", NULL);
    if (node->returnStmt.expr) {
        format(ctx->state, " ", NULL);
        astConstVisit(visitor, node->returnStmt.expr);
    }
    format(ctx->state, ";", NULL);
}

void cCodegenEpilogue(CCodegenContext *context, const AstNode *prog)
{
    // clang-format off
    ConstAstVisitor visitor = makeConstAstVisitor(context,
    {
        [astProgram] = programEpilogue,
        [astPathElem] = generatePathElement,
        [astPath] = generatePath,
        [astIdentifier] = generateIdentifier,
        [astNullLit] = generateLiteral,
        [astBoolLit] = generateLiteral,
        [astCharLit] = generateLiteral,
        [astIntegerLit] = generateLiteral,
        [astFloatLit] = generateLiteral,
        [astStringLit] = generateLiteral,
        [astAddressOf] = generateAddressOf,
        [astStmtExpr] = generateStatementExpr,
        [astTupleExpr] = generateTupleExpr,
        [astMemberExpr] = generateMemberExpr,
        [astCallExpr] = generateCallExpr,
        [astBlockStmt] = generateBlock,
        [astExprStmt] = generateExpressionStmt,
        [astReturnStmt] = generateReturn,
        [astFuncParam] = generateFuncParam,
        [astFuncDecl] = generateFunc,
        [astVarDecl] = generateVariable,
        [astConstDecl] = generateVariable,
        [astTypeDecl] = generateTypeDecl
    },

    .fallback = generateCCodeFallback);

    astConstVisit(&visitor, prog);
}
