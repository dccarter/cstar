//
// Created by Carter Mbotho on 2024-03-12.
//

#include "preprocessor.h"

#include "driver/driver.h"

#include "lang/frontend/ast.h"
#include "lang/frontend/defines.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/visitor.h"

#include "lang/middle/scope.h"

static void substituteAstNode(AstVisitor *visitor,
                              AstNode *node,
                              AstNode *with,
                              bool clone)
{
    PreprocessorContext *ctx = getAstVisitorContext(visitor);
    AstNode *body =
        (clone && nodeIs(with, MacroDecl)) ? with->macroDecl.body : with;
    AstNodeList replacements = {};
    for (; body;) {
        AstNode *substitute = clone ? deepCloneAstNode(ctx->pool, body) : body;
        body = body->next;
        substitute->parentScope = node->parentScope;
        substitute->next = NULL;
        astVisit(visitor, substitute);
        insertAstNode(&replacements, substitute);
    }
    replaceAstNode(node, replacements.first);
}

static void visitBinaryExpr(AstVisitor *visitor, AstNode *node)
{
    PreprocessorContext *ctx = getAstVisitorContext(visitor);
    AstNode *lhs = node->binaryExpr.lhs;
    AstNode *rhs = node->binaryExpr.rhs;
    astVisit(visitor, lhs);
    astVisit(visitor, rhs);

    if (!isNumericLiteral(lhs) || !isNumericLiteral(rhs))
        return;

    preprocessorEvalBinaryExpr(ctx, node);
}

static void visitUnaryExpr(AstVisitor *visitor, AstNode *node)
{
    PreprocessorContext *ctx = getAstVisitorContext(visitor);
    Operator op = node->unaryExpr.op;
    AstNode *operand = node->unaryExpr.operand;
    astVisit(visitor, operand);
    if (op != opNot && op != opMinus && op != opPlus && op != opCompl)
        return;

    if (!isNumericLiteral(operand))
        return;

    preprocessorEvalUnaryExpr(ctx, node);
}

static void visitTernaryExpr(AstVisitor *visitor, AstNode *node)
{
    PreprocessorContext *ctx = getAstVisitorContext(visitor);
    AstNode *cond = node->ternaryExpr.cond, *then = node->ternaryExpr.body,
            *els = node->ternaryExpr.otherwise;

    astVisit(visitor, cond);
    if (!preprocessorAsBoolean(ctx, cond)) {
        astVisit(visitor, then);
        astVisit(visitor, els);
        return;
    }

    if (cond->boolLiteral.value)
        substituteAstNode(visitor, node, then, false);
    else
        substituteAstNode(visitor, node, els, false);
}

static void visitMacroCall(AstVisitor *visitor, AstNode *node)
{
    PreprocessorContext *ctx = getAstVisitorContext(visitor);
    AstNode *callee = node->callExpr.callee, *args = node->callExpr.args;
    AstNode *macro =
        preprocessorFindMacro(ctx->preprocessor, callee->ident.value)
            ?: findSymbolOnly(ctx->env, callee->ident.value);

    astVisitManyNodes(visitor, args);
    if (macro == NULL)
        return;

    if (!nodeIs(macro, MacroDecl)) {
        substituteAstNode(visitor, node, macro, true);
        node->flags |= flgSubstituted;
        return;
    }

    if (macro->macroDecl.body == NULL) {
        node->tag = astNoop;
        clearAstBody(node);
        node->flags |= flgSubstituted;
        return;
    }

    u64 paramsCount = countAstNodes(macro->macroDecl.params);
    u64 argumentsCount = countAstNodes(args);
    u64 requiredArguments = paramsCount - hasFlag(macro, Variadic);
    if (argumentsCount < requiredArguments) {
        logError(
            ctx->L,
            &node->loc,
            "insufficient number of arguments passed to macro {s}, got {u64}, "
            "expecting {u64}",
            (FormatArg[]){{.s = macro->macroDecl.name},
                          {.u64 = argumentsCount},
                          {.u64 = paramsCount}});
        logNote(ctx->L, &macro->loc, "macro declared here", NULL);
        return;
    }

    if (!hasFlag(macro, Variadic) && argumentsCount > requiredArguments) {
        logError(ctx->L,
                 &node->loc,
                 "to many arguments passed to macro, expecting {u64}",
                 (FormatArg[]){{.u64 = paramsCount}});
        logNote(ctx->L, &macro->loc, "macro declared here", NULL);
        return;
    }

    AstNode *param = macro->macroDecl.params, *arg = args;
    pushScope(ctx->env, node);
    bool ok = true;
    for (; arg && param; param = param->next) {
        AstNode *tmp = arg;
        arg = arg->next;
        if (!hasFlag(param, Variadic))
            tmp->next = NULL;
        //        else
        //            tmp = makeTupleExpr(
        //                ctx->pool, &tmp->loc, flgVariadic, tmp, NULL, NULL);

        tmp->flags |= flgMacroArgument;
        astVisit(visitor, tmp);
        ok = defineSymbol(ctx->env, ctx->L, param->ident.value, tmp) && ok;
    }

    if (ok && macro->macroDecl.body) {
        typeof(ctx->stack) stack = ctx->stack;
        ctx->stack = (typeof(ctx->stack)){node->loc, true};
        substituteAstNode(visitor, node, macro, true);
        ctx->stack = stack;
        node->flags |= flgSubstituted;
    }
    popScope(ctx->env);
}

static void visitExprStmt(AstVisitor *visitor, AstNode *node)
{
    PreprocessorContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->exprStmt.expr;
    astVisit(visitor, expr);
    if (hasFlag(expr, Substituted))
        replaceAstNode(node, expr);
}

static void visitBlockStmt(AstVisitor *visitor, AstNode *node)
{
    AstNode *stmt = node->blockStmt.stmts;
    AstNodeList stmts = {};
    for (; stmt;) {
        AstNode *tmp = stmt;
        stmt = stmt->next;
        astVisitManyNodes(visitor, tmp);

        tmp->next = NULL;
        if (!nodeIsNoop(tmp))
            insertAstNode(&stmts, tmp);
    }
    node->blockStmt.stmts = stmts.first;
}

static void visitIfStmt(AstVisitor *visitor, AstNode *node)
{
    PreprocessorContext *ctx = getAstVisitorContext(visitor);
    AstNode *cond = node->ifStmt.cond, *then = node->ifStmt.body,
            *els = node->ifStmt.otherwise;

    astVisit(visitor, cond);
    if (hasFlag(node, Comptime)) {
        // Check if we can pre-process
        if (preprocessorAsBoolean(ctx, cond)) {
            if (cond->boolLiteral.value) {
                substituteAstNode(visitor, node, then, false);
            }
            else if (els != NULL) {
                substituteAstNode(visitor, node, els, false);
            }
            else {
                node->tag = astNoop;
            }
            return;
        }
    }
    astVisit(visitor, then);
    if (els != NULL)
        astVisit(visitor, els);
}

static void visitMacroDecl(AstVisitor *visitor, AstNode *node)
{
    PreprocessorContext *ctx = getAstVisitorContext(visitor);
    AstNode *previous = NULL;
    if (!preprocessorOverrideDefinedMacro(
            ctx->preprocessor, node->macroDecl.name, node, &previous)) //
    {
        logWarning(ctx->L,
                   &node->loc,
                   "overriding macro with name '{s}' which was already defined",
                   (FormatArg[]){{.s = node->macroDecl.name}});
        if (previous && previous->loc.fileName != NULL) {
            logNote(
                ctx->L, &previous->loc, "macro previously declared here", NULL);
        }
    }
}

// static void visitProgram(AstVisitor *visitor, AstNode *node)
//{
//     PreprocessorContext *ctx = getAstVisitorContext(visitor);
//     astVisitFallbackVisitAll(node->program.top);
// }

static void dispatch(Visitor func, AstVisitor *visitor, AstNode *node)
{
    PreprocessorContext *ctx = getAstVisitorContext(visitor);
    if (ctx->resetLocation)
        node->loc = ctx->loc;

    func(visitor, node);
}

AstNode *preprocessAst(CompilerDriver *driver, AstNode *node)
{
    Env env;
    environmentInit(&env, node);
    PreprocessorContext context = {.env = &env,
                                   .L = driver->L,
                                   .pool = driver->pool,
                                   .preprocessor = &driver->preprocessor};

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context, {
        //[astProgram] = visitProgram,
        [astMacroCallExpr] = visitMacroCall,
        [astExprStmt] = visitExprStmt,
        [astBlockStmt] = visitBlockStmt,
        [astUnaryExpr] = visitUnaryExpr,
        [astBinaryExpr] = visitBinaryExpr,
        [astTernaryExpr] = visitTernaryExpr,
        [astIfStmt] = visitIfStmt,
        [astMacroDecl] = visitMacroDecl,
    }, .fallback = astVisitFallbackVisitAll, .dispatch = dispatch);
    // clang-format on

    astVisit(&visitor, node);

    environmentFree(&env);

    return node;
}
