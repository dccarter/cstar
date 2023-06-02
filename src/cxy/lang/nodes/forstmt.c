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
#include "lang/eval.h"
#include "lang/semantics.h"

#include "lang/ast.h"
#include "lang/node.h"
#include "lang/ttable.h"

#include <memory.h>

static void checkForStmtGenerator(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode orig = *node;

    AstNode *range = orig.forStmt.range;
    AstNode *args = range->callExpr.args ? getLastAstNode(range->callExpr.args)
                                         : NULL,
            *arg = NULL, *names = orig.forStmt.var->varDecl.names,
            *closureArgs = NULL;
    u64 varCount = countAstNodes(names), i = 1,
        argsCount = countAstNodes(range->callExpr.args);
    const Type *bodyFunc = NULL;
    const FileLoc *loc = args ? &args->loc : &node->loc;
    //        const Type *callee = evalType(visitor, range->callExpr.callee),
    //                   *bodyFunc = NULL;
    //
    //        if (callee->tag != typFunc || callee->func.paramsCount == 0 ||
    //            !hasFlag(callee->func.params[callee->func.paramsCount - 1],
    //                     FuncTypeParam)) {
    //            logError(ctx->L,
    //                     &range->callExpr.callee->loc,
    //                     "for range expression is not a generator function",
    //                     NULL);
    //
    //            node->type = ERROR_TYPE(ctx);
    //            return;
    //        }

    SymbolRef *symbols = findSymbolRefByNode(
                  ctx, ctx->env, range->callExpr.callee, true),
              *symbol = symbols;
    csAssert0(symbols);

    bool reportErrors = false;
findMatchingRangeFunc:
    for (; symbol; symbol = symbol->next) {
        const Type *type = symbol->node->type;
        if (!typeIs(type, Func) || type->func.paramsCount != 1)
            continue;
        if (hasFlag(range->callExpr.callee, Const) &&
            !hasFlag(symbol->node, Const)) {
            if (reportErrors) {
                logNote(ctx->L,
                        &symbol->node->loc,
                        "found candidate function here, operating on a const "
                        "target requires a constant function",
                        NULL);
            }
            continue;
        }

        const Type *last = type->func.params[type->func.paramsCount - 1];
        if (!hasFlag(last, FuncTypeParam)) {
            if (reportErrors) {
                logNote(ctx->L,
                        &symbol->node->loc,
                        "found candidate function here, declaration cannot be "
                        "used as range operator, last argument must be a "
                        "function type",
                        NULL);
            }
            continue;
        }

        bodyFunc = last->tuple.members[1];
        if (varCount != bodyFunc->func.paramsCount - 1) {
            if (reportErrors) {
                logNote(ctx->L,
                        &node->forStmt.var->loc,
                        "found candidate function here, for loop variable "
                        "declaration mismatch, declared {u64}, "
                        "expecting {u64}",
                        (FormatArg[]){{.u64 = varCount},
                                      {.u64 = bodyFunc->func.paramsCount}});
            }

            bodyFunc = NULL;
            continue;
        }
        // override the overload lookup
        range->callExpr.overload = symbol->node->funcDecl.index + 1;
        break;
    }

    if (reportErrors) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (bodyFunc == NULL) {
        reportErrors = true;
        symbol = symbols;
        logError(ctx->L,
                 &range->callExpr.callee->loc,
                 "couldn't find a viable candidate range overload operator",
                 NULL);
        goto findMatchingRangeFunc;
    }

    for (AstNode *name = names; name; name = name->next, i++) {
        AstNode *newArg = makeAstNode(
            ctx->pool,
            loc,
            &(AstNode){.type = bodyFunc->func.params[i],
                       .tag = astFuncParam,
                       .funcParam = {
                           .name = isIgnoreVar(name->ident.value)
                                       ? makeAnonymousVariable(ctx->strPool, "")
                                       : name->ident.value}});
        if (closureArgs == NULL) {
            closureArgs = newArg;
            arg = newArg;
        }
        else {
            arg->next = newArg;
            arg = newArg;
        }
    }

    AstNode *next =
        makeAstNode(ctx->pool,
                    loc,
                    &(AstNode){.tag = astClosureExpr,
                               .closureExpr = {.params = closureArgs,
                                               .body = node->forStmt.body}});
    if (args != NULL)
        args->next = next;
    else
        orig.forStmt.range->callExpr.args = next;

    memset(&node->forStmt, 0, sizeof(node->forStmt));
    node->tag = astCallExpr;
    node->callExpr = orig.forStmt.range->callExpr;

    evalType(visitor, node);
}

static void checkForStmtRangeOperator(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *range = stripAll(node->forStmt.range->type);
    SymbolRef *symbol =
        findSymbolRef(range->tStruct.env, NULL, "op_range", NULL);
    if (symbol == NULL) {
        logError(ctx->L,
                 &node->forStmt.range->loc,
                 "expression of type '{t}' does not implement the range "
                 "operator `..` for use in for loop",
                 (FormatArg[]){{.t = range}});

        node->type = ERROR_TYPE(ctx);
        return;
    }

    AstNode *callee = makeAstNode(
        ctx->pool,
        &node->forStmt.range->loc,
        &(AstNode){.tag = astMemberExpr,
                   .flags = node->forStmt.range->flags,
                   .memberExpr = {
                       .target = copyAstNode(ctx->pool, node->forStmt.range),
                       .member = makeAstNode(
                           ctx->pool,
                           &node->forStmt.range->loc,
                           &(AstNode){.tag = astIdentifier,
                                      .flags = node->forStmt.range->type->flags,
                                      .ident.value = "op_range"})}});

    memset(&node->forStmt.range->_body, 0, CXY_AST_NODE_BODY_SIZE);
    node->forStmt.range->tag = astCallExpr;
    node->forStmt.range->type = NULL;
    node->forStmt.range->callExpr.callee = callee;
    node->forStmt.range->callExpr.args = NULL;

    checkForStmtGenerator(visitor, node);
}

void checkForStmt(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    if (node->forStmt.range->tag == astCallExpr) {
        checkForStmtGenerator(visitor, node);
        return;
    }

    const Type *range = evalType(visitor, node->forStmt.range),
               *stripped = stripAll(range);

    if (typeIs(stripped, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (typeIs(stripped, Struct)) {
        checkForStmtRangeOperator(visitor, node);
        return;
    }

    pushScope(ctx->env, node);
    const Type *type = evalType(visitor, node->forStmt.var);
    if (typeIs(type, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    AstNode *symbol = findSymbol(ctx->env,
                                 ctx->L,
                                 node->forStmt.var->varDecl.names->ident.value,
                                 &node->loc);
    csAssert0(symbol);
    if (node->forStmt.range->tag == astRangeExpr) {
        if (type->tag != typAuto && !isIntegerType(type)) {
            logError(ctx->L,
                     &node->forStmt.var->loc,
                     "unexpected type for loop variable type '{t}', expecting "
                     "an integral type",
                     (FormatArg[]){{.t = type}});
            type = ERROR_TYPE(ctx);
        }
        else if (type->tag == typAuto) {
            symbol->type = getPrimitiveType(ctx->typeTable, prtI64);
            node->forStmt.var->type = symbol->type;
        }
    }
    else {
        range = stripped;

        if (range->tag == typArray) {
            const Type *elementType = range->array.elementType;
            //            node->forStmt.range = makeAstNode(
            //                ctx->pool,
            //                &node->forStmt.range->loc,
            //                &(AstNode){.tag = astUnaryExpr,
            //                           .type = range,
            //                           .flags = node->forStmt.range->flags,
            //                           .unaryExpr = {.op = opDeref,
            //                                         .operand =
            //                                         node->forStmt.range,
            //                                         .isPrefix = true}});
            if (type->tag != typAuto &&
                !isTypeAssignableFrom(elementType, type)) {
                logError(ctx->L,
                         &node->forStmt.var->loc,
                         "unexpected type '{t}' for loop variable, "
                         "expecting array "
                         "element type '{t}'",
                         (FormatArg[]){{.t = type}, {.t = elementType}});
                type = ERROR_TYPE(ctx);
            }
            else if (type->tag == typAuto) {
                symbol->type = elementType;
                node->forStmt.var->type = elementType;
            }
        }
        else {
            unreachable("");
        }
    }

    const Type *body = evalType(visitor, node->forStmt.body);

    node->type = type == ERROR_TYPE(ctx) ? type : body;

    popScope(ctx->env);
}

void generateForStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    const AstNode *range = node->forStmt.range;

    if (range->tag == astRangeExpr) {
        generateForStmtRange(visitor, node);
    }
    else if (range->type->tag == typArray) {
        generateForStmtArray(visitor, node);
    }
    else {
        unreachable("currently not supported");
    }
}

void evalForStmt(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    FileLoc rangeLoc = node->forStmt.range->loc;

    if (!evaluate(visitor, node->forStmt.range)) {
        node->tag = astError;
        return;
    }

    AstNode *range = node->forStmt.range;
    if (!hasFlag(range, ComptimeIterable)) {
        logError(ctx->L,
                 &rangeLoc,
                 "`#for` loop range expression is not comptime iterable",
                 NULL);
        node->tag = astError;
        return;
    }

    AstNode *it = nodeIs(range, ComptimeOnly) ? range->next : range;
    // open a new scope
    AstNodeList nodes = {NULL};
    cstring name = node->forStmt.var->varDecl.names->ident.value;

    while (it) {
        AstNode *body = cloneAstNode(ctx->pool, node->forStmt.body);

        pushScope(&ctx->eval.env, node);
        SymbolRef *ref = updateSymbol(&ctx->eval.env, name, it);
        if (!evaluate(ctx->eval.semanticsVisitor, body) ||
            typeIs(body, Error)) {
            node->tag = astError;
            popScope(&ctx->eval.env);
            return;
        }
        popScope(&ctx->eval.env);

        if (!nodeIs(it, Nop))
            insertAstNode(&nodes, body);

        it = it->next;
        ref->node = it;
    }

    if (nodes.first == NULL) {
        node->tag = astNop;
    }
    else {
        nodes.last->next = node->next;
        *node = *nodes.first;
    }
}
