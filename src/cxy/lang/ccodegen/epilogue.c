//
// Created by Carter on 2023-03-29.
//

#include "lang/ttable.h"

#include "lang/codegen.h"

#include "core/alloc.h"

#include <string.h>

#define CXY_EPILOGUE_SRC_FILE CXY_SOURCE_LANG_DIR "/ccodegen/epilogue.cxy.c"

static void programEpilogue(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    size_t bytes = 0;
    format(ctx->state,
           "\n"
           "/* --------------------- Generated EPILOGUE --------------*/\n"
           "\n",
           NULL);

    generateManyAsts(visitor, "\n\n", node->program.decls);

    format(ctx->state,
           "\n"
           "/* --------------------- epilogue.cxy.c --------------*/\n"
           "\n",
           NULL);

    append(ctx->state, readFile(CXY_EPILOGUE_SRC_FILE, &bytes), bytes);
    format(ctx->state, "\n", NULL);
}

static void generateIdentifier(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "{s}", (FormatArg[]){{.s = node->ident.value}});
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

static void generateAssignExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->assignExpr.lhs);
    format(ctx->state,
           " {s} ",
           (FormatArg[]){{.s = getAssignOpString(node->assignExpr.op)}});
    astConstVisit(visitor, node->assignExpr.rhs);
}

static void generateGroupExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "(", NULL);
    astConstVisit(visitor, node->groupExpr.expr);
    format(ctx->state, ")", NULL);
}

static void generateTypedExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "(", NULL);
    generateTypeUsage(ctx, node->type);
    format(ctx->state, ")", NULL);
    astConstVisit(visitor, node->typedExpr.expr);
}

static void generateCastExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "(", NULL);
    generateTypeUsage(ctx, node->castExpr.to->type);
    format(ctx->state, ")", NULL);
    astConstVisit(visitor, node->castExpr.expr);
}

static void generateTernaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->ternaryExpr.cond);
    format(ctx->state, "? ", NULL);
    astConstVisit(visitor, node->ternaryExpr.body);
    format(ctx->state, ": ", NULL);
    astConstVisit(visitor, node->ternaryExpr.otherwise);
}

static void generateBlock(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *ret = NULL;
    const AstNode *epilogue = node->blockStmt.epilogue.first;

    format(ctx->state, "{{{>}\n", NULL);
    for (const AstNode *stmt = node->blockStmt.stmts; stmt; stmt = stmt->next) {
        if (epilogue && stmt->tag == astReturnStmt) {
            ret = stmt;
            continue;
        }
        astConstVisit(visitor, stmt);
        if (stmt->tag == astCallExpr)
            format(ctx->state, ";", NULL);
        if (epilogue || stmt->next)
            format(ctx->state, "\n", NULL);
    }

    for (; epilogue; epilogue = epilogue->next) {
        astConstVisit(visitor, epilogue);
        if ((epilogue->flags & flgDeferred) && epilogue->tag != astBlockStmt)
            format(ctx->state, ";", NULL);

        if (ret || epilogue->next)
            format(ctx->state, "\n", NULL);
    }

    if (ret)
        astConstVisit(visitor, ret);
    format(ctx->state, "{<}\n}", NULL);
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

static void generateBreakContinue(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (node->tag == astBreakStmt)
        format(ctx->state, "break;", NULL);
    else
        format(ctx->state, "continue;", NULL);
}

static void generateIfStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *cond = node->ifStmt.cond;
    if (cond->tag == astVarDecl) {
        format(ctx->state, "{{{>}\n", NULL);
        astConstVisit(visitor, cond);
        format(ctx->state,
               "\nif ({s}) ",
               (FormatArg[]){{.s = cond->varDecl.names->ident.value}});
    }
    else {
        format(ctx->state, "if (", NULL);
        astConstVisit(visitor, cond);
        format(ctx->state, ") ", NULL);
    }
    astConstVisit(visitor, node->ifStmt.body);
    if (node->ifStmt.otherwise) {
        format(ctx->state, " else ", NULL);
        astConstVisit(visitor, node->ifStmt.otherwise);
    }

    if (cond->tag == astVarDecl) {
        format(ctx->state, "{<}\n}", NULL);
    }
}

static void generateWhileStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *cond = node->whileStmt.cond;
    if (cond->tag == astVarDecl) {
        format(ctx->state, "{{{>}\n", NULL);
        astConstVisit(visitor, cond);
        format(ctx->state,
               "\nwhile ({s}) ",
               (FormatArg[]){{.s = cond->varDecl.names->ident.value}});
    }
    else {
        format(ctx->state, "while (", NULL);
        astConstVisit(visitor, cond);
        format(ctx->state, ") ", NULL);
    }
    astConstVisit(visitor, node->whileStmt.body);

    if (cond->tag == astVarDecl) {
        format(ctx->state, "{<}\n}", NULL);
    }
}

void codegenEpilogue(CodegenContext *context, const AstNode *prog)
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
        [astBinaryExpr] = generateBinaryExpr,
        [astUnaryExpr] = generateUnaryExpr,
        [astAssignExpr] = generateAssignExpr,
        [astTupleExpr] = generateTupleExpr,
        [astStructExpr] = generateStructExpr,
        [astArrayExpr] = generateArrayExpr,
        [astMemberExpr] = generateMemberExpr,
        [astCallExpr] = generateCallExpr,
        [astStringExpr] = generateStringExpr,
        [astGroupExpr] = generateGroupExpr,
        [astTypedExpr] = generateTypedExpr,
        [astCastExpr] = generateCastExpr,
        [astIndexExpr] = generateIndexExpr,
        [astTernaryExpr] = generateTernaryExpr,
        [astNewExpr] = generateNewExpr,
        [astBlockStmt] = generateBlock,
        [astExprStmt] = generateExpressionStmt,
        [astReturnStmt] = generateReturn,
        [astBreakStmt] = generateBreakContinue,
        [astContinueStmt] = generateBreakContinue,
        [astIfStmt] = generateIfStmt,
        [astWhileStmt] = generateWhileStmt,
        [astForStmt] = generateForStmt,
        [astFuncParam] = generateFuncParam,
        [astFuncDecl] = generateFunctionDefinition,
        [astVarDecl] = generateVariableDecl,
        [astTypeDecl] = generateTypeDecl
    },

    .fallback = generateFallback);

    astConstVisit(&visitor, prog);
}
