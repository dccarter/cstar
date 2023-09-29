//
// Created by Carter on 2023-08-31.
//

#include "../check.h"
#include "../codegen.h"
#include "../eval.h"

#include "lang/capture.h"
#include "lang/flag.h"
#include "lang/operations.h"
#include "lang/strings.h"
#include "lang/ttable.h"
#include "lang/visitor.h"

#include "core/alloc.h"

void generateIfStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *cond = node->ifStmt.cond;

    format(ctx->state, "if (", NULL);
    astConstVisit(visitor, cond);
    format(ctx->state, ") ", NULL);

    astConstVisit(visitor, node->ifStmt.body);

    if (node->ifStmt.otherwise) {
        format(ctx->state, " else ", NULL);
        astConstVisit(visitor, node->ifStmt.otherwise);
    }
}

void checkIfStmt(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *cond = node->ifStmt.cond, *then = node->ifStmt.body,
            *otherwise = node->ifStmt.otherwise;

    const Type *cond_ = checkType(visitor, cond);
    if (typeIs(cond_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    cond_ = unwrapType(cond_, NULL);
    if (isClassOrStructType(cond_)) {
        if (!transformToTruthyOperator(visitor, cond)) {
            if (!typeIs(cond->type, Error))
                logError(ctx->L,
                         &cond->loc,
                         "expecting a struct that overloads the truthy `!!` in "
                         "an if statement condition, "
                         "got '{t}'",
                         (FormatArg[]){{.t = cond_}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
        cond_ = cond->type;
    }

    if (!isTruthyType(cond_)) {
        logError(
            ctx->L,
            &cond->loc,
            "expecting a truthy type in an if statement condition, got '{t}'",
            (FormatArg[]){{.t = cond_}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    const Type *then_ = checkType(visitor, then);
    if (typeIs(then_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (otherwise == NULL) {
        node->type = then_;
        return;
    }

    const Type *otherwise_ = checkType(visitor, otherwise);
    if (typeIs(otherwise_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (typeIs(then_, Auto) || typeIs(otherwise_, Auto)) {
        node->type = typeIs(then_, Auto) ? otherwise_ : then_;
        return;
    }

    if (!isTypeAssignableFrom(then_, otherwise_)) {
        logError(ctx->L,
                 &otherwise->loc,
                 "inconsistent return type on if statement branches, then type "
                 "'{t}' is not assignable to else type '{t}'",
                 (FormatArg[]){{.t = then_}, {.t = otherwise_}});
        node->type = ERROR_TYPE(ctx);
        return;
    }
    node->type = then_;
}

void evalIfStmt(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);

    AstNode *cond = node->ifStmt.cond;
    if (!evaluate(visitor, cond) || !evalBooleanCast(ctx, cond)) {
        node->tag = astError;
        return;
    }
    bool flatten = findAttribute(node, S_consistent) == NULL;

    AstNode *next = node->next;
    u64 visited = node->flags & flgVisited;

    if (cond->boolLiteral.value) {
        // select then branch & reclaim else branch if any
        AstNode *replacement = node->ifStmt.body;
        if (flatten && nodeIs(replacement, BlockStmt))
            replacement = replacement->blockStmt.stmts
                              ?: makeAstNop(ctx->pool, &replacement->loc);
        replaceAstNodeWith(node, replacement);
    }
    else if (node->ifStmt.otherwise) {
        // select otherwise, reclaim if branch
        AstNode *replacement = node->ifStmt.otherwise;
        if (flatten && nodeIs(replacement, BlockStmt))
            replacement = replacement->blockStmt.stmts
                              ?: makeAstNop(ctx->pool, &replacement->loc);
        replaceAstNodeWith(node, replacement);
    }
    else {
        // select next statement, reclaim if branch
        if (next)
            *node = *next;
        else {
            clearAstBody(node);
            node->tag = astNop;
            node->flags &= ~flgComptime;
        }
    }

    while (nodeIs(node, IfStmt) && hasFlag(node, Comptime)) {
        node->flags &= ~flgComptime;
        if (!evaluate(visitor, node)) {
            node->tag = astError;
            return;
        }
    }

    node->flags |= visited;
}
