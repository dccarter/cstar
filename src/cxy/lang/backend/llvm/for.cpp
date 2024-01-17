//
// Created by Carter Mbotho on 2024-01-13.
//

#include "context.h"

extern "C" {
#include "lang/frontend/flag.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/visitor.h"
}

static void generateForRangeStmt(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = LLVMContext::from(visitor);
    auto range = node->forStmt.range, var = node->forStmt.var,
         body = node->forStmt.body;

    var->varDecl.init = range->rangeExpr.start;
    auto condition = makeBinaryExpr(ctx.pool,
                                    &range->loc,
                                    flgNone,
                                    makeResolvedPath(ctx.pool,
                                                     &range->loc,
                                                     var->varDecl.name,
                                                     flgNone,
                                                     var,
                                                     nullptr,
                                                     var->type),
                                    opNe,
                                    range->rangeExpr.end,
                                    nullptr,
                                    getPrimitiveType(ctx.types, prtBool));
    auto advance = makeExprStmt(
        ctx.pool,
        &node->loc,
        flgNone,
        makeAssignExpr(
            ctx.pool,
            &range->loc,
            flgNone,
            makeResolvedPath(ctx.pool,
                             &range->loc,
                             var->varDecl.name,
                             flgNone,
                             var,
                             nullptr,
                             var->type),
            opAdd,
            range->rangeExpr.step
                ?: makeIntegerLiteral(
                       ctx.pool, &range->loc, 1, nullptr, range->type),
            nullptr,
            range->type),
        nullptr,
        range->type);

    var->next = makeWhileStmt(ctx.pool,
                              &node->loc,
                              node->flags,
                              condition,
                              body,
                              nullptr,
                              body->type);
    var->next->whileStmt.update = advance;

    node->tag = astBlockStmt;
    node->blockStmt.stmts = var;
    node->blockStmt.last = var->next;
    astVisit(visitor, node);
}

static void generateForArrayStmt(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = LLVMContext::from(visitor);
    auto range = node->forStmt.range, var = node->forStmt.var,
         body = node->forStmt.body;
    auto elem = typeIs(range->type, String)
                    ? getPrimitiveType(ctx.types, prtChar)
                    : range->type->array.elementType;

    auto index = var->next
                     ?: makeVarDecl(ctx.pool,
                                    &var->loc,
                                    flgConst,
                                    makeAnonymousVariable(ctx.strings, "i"),
                                    nullptr,
                                    makeIntegerLiteral(
                                        ctx.pool,
                                        &var->loc,
                                        0,
                                        nullptr,
                                        getPrimitiveType(ctx.types, prtU64)),
                                    nullptr,
                                    getPrimitiveType(ctx.types, prtU64));
    if (var->next) {
        var->next->varDecl.init =
            makeIntegerLiteral(ctx.pool,
                               &var->loc,
                               0,
                               nullptr,
                               getPrimitiveType(ctx.types, prtU64));
    }
    else {
        var->next = index;
    }

    range = makeVarDecl(ctx.pool,
                        &range->loc,
                        range->flags,
                        "",
                        nullptr,
                        range,
                        nullptr,
                        range->type);
    index->next = range;

    auto condition =
        makeBinaryExpr(ctx.pool,
                       &range->loc,
                       flgNone,
                       makeResolvedPath(ctx.pool,
                                        &range->loc,
                                        index->varDecl.name,
                                        flgNone,
                                        index,
                                        nullptr,
                                        index->type),
                       opLt,
                       makeIntegerLiteral(ctx.pool,
                                          &range->loc,
                                          range->type->array.len,
                                          nullptr,
                                          getPrimitiveType(ctx.types, prtU64)),
                       nullptr,
                       getPrimitiveType(ctx.types, prtBool));

    auto assign = makeExprStmt(
        ctx.pool,
        &range->loc,
        flgNone,
        makeAssignExpr(ctx.pool,
                       &range->loc,
                       flgNone,
                       makeResolvedPath(ctx.pool,
                                        &range->loc,
                                        var->varDecl.name,
                                        flgNone,
                                        var,
                                        nullptr,
                                        var->type),
                       opAssign,
                       makeIndexExpr(ctx.pool,
                                     &range->loc,
                                     elem->flags,
                                     makeResolvedPath(ctx.pool,
                                                      &range->loc,
                                                      range->varDecl.name,
                                                      flgNone,
                                                      range,
                                                      nullptr,
                                                      range->type),
                                     makeResolvedPath(ctx.pool,
                                                      &range->loc,
                                                      index->varDecl.name,
                                                      flgNone,
                                                      index,
                                                      nullptr,
                                                      index->type),
                                     nullptr,
                                     var->type),
                       nullptr,
                       range->type),
        nullptr,
        var->type);

    auto advance = makeExprStmt(
        ctx.pool,
        &node->loc,
        flgNone,
        makeAssignExpr(
            ctx.pool,
            &range->loc,
            flgNone,
            makeResolvedPath(ctx.pool,
                             &range->loc,
                             index->varDecl.name,
                             flgNone,
                             index,
                             nullptr,
                             index->type),
            opAdd,
            makeIntegerLiteral(ctx.pool, &range->loc, 1, nullptr, index->type),
            nullptr,
            index->type),
        nullptr,
        index->type);
    assign->next = advance;

    if (nodeIs(body, BlockStmt)) {
        if (body->blockStmt.stmts) {
            assign->next = body->blockStmt.stmts;
            body->blockStmt.stmts = assign;
        }
        else
            body->blockStmt.stmts = assign;
    }
    else {
        assign->next = body;
        body =
            makeBlockStmt(ctx.pool, &assign->loc, assign, nullptr, node->type);
    }

    range->next = makeWhileStmt(ctx.pool,
                                &node->loc,
                                node->flags,
                                condition,
                                body,
                                nullptr,
                                body->type);
    range->next->whileStmt.update = advance;
    node->tag = astBlockStmt;
    node->blockStmt.stmts = var;
    node->blockStmt.last = range->next;
    astVisit(visitor, node);
}

void visitForStmt(AstVisitor *visitor, AstNode *node)
{
    auto range = node->forStmt.range;
    if (nodeIs(range, RangeExpr))
        generateForRangeStmt(visitor, node);
    else
        generateForArrayStmt(visitor, node);
}