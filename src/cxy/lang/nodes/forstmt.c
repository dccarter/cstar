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

#include <memory.h>

static void checkForStmtGenerator(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode orig = *node;

    AstNode *range = orig.forStmt.range;
    AstNode *args = getLastAstNode(range->callExpr.args), *arg = NULL,
            *names = orig.forStmt.var->varDecl.names, *closureArgs = NULL;
    u64 varCount = countAstNodes(names), i = 1;

    const Type *callee = evalType(visitor, range->callExpr.callee),
               *bodyFunc = NULL;

    if (callee->tag != typFunc || callee->func.paramsCount == 0 ||
        callee->func.params[callee->func.paramsCount - 1]->flags !=
            flgFuncTypeParam) {
        logError(ctx->L,
                 &range->callExpr.callee->loc,
                 "for range expression is not a generator functions",
                 NULL);

        node->type = ERROR_TYPE(ctx);
        return;
    }

    bodyFunc =
        callee->func.params[callee->func.paramsCount - 1]->tuple.members[1];
    if (varCount != bodyFunc->func.paramsCount - 1) {
        logError(ctx->L,
                 &node->forStmt.var->loc,
                 "for loop variable declaration mismatch, declared {u64}, "
                 "expecting {u64}",
                 (FormatArg[]){{.u64 = varCount},
                               {.u64 = bodyFunc->func.paramsCount}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    for (AstNode *name = names; name; name = name->next, i++) {
        AstNode *newArg = makeAstNode(
            ctx->pool,
            &args->loc,
            &(AstNode){.type = bodyFunc->func.params[i],
                       .tag = astFuncParam,
                       .funcParam = {.name = name->ident.value, .type = NULL}});
        if (closureArgs == NULL) {
            closureArgs = newArg;
            arg = newArg;
        }
        else {
            arg->next = newArg;
            arg = newArg;
        }
    }

    args->next =
        makeAstNode(ctx->pool,
                    &args->loc,
                    &(AstNode){.tag = astClosureExpr,
                               .closureExpr = {.params = closureArgs,
                                               .body = node->forStmt.body}});

    memset(&node->forStmt, 0, sizeof(node->forStmt));
    node->tag = astCallExpr;
    node->callExpr = orig.forStmt.range->callExpr;

    evalType(visitor, node);
}

static void checkForStmtRangeOperator(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *range = stripPointer(node->forStmt.range->type);
    AstNode *rangeOp = findSymbolOnly(range->tStruct.env, "op_range");
    if (rangeOp == NULL) {
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
        &(AstNode){
            .tag = astMemberExpr,
            .flags = node->forStmt.range->flags,
            .memberExpr = {.target = copyAstNode(ctx->pool, node),
                           .member = makeAstNode(
                               ctx->pool,
                               &node->forStmt.range->loc,
                               &(AstNode){.tag = astIdentifier,
                                          .flags = node->forStmt.range->flags,
                                          .ident.value = "op_range"})}});

    memset(&node->_body, 0, CYX_AST_NODE_BODY_SIZE);
    node->tag = astCallExpr;
    node->type = NULL;
    node->callExpr.callee = callee;
    node->callExpr.args = NULL;

    checkForStmtGenerator(visitor, node);
}

void checkForStmt(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    if (node->forStmt.range->tag == astCallExpr) {
        checkForStmtGenerator(visitor, node);
        return;
    }

    const Type *range = evalType(visitor, node->forStmt.range);
    if (stripPointer(range)->tag == typStruct) {
        checkForStmtRangeOperator(visitor, node);
        return;
    }

    pushScope(&ctx->env, node);
    const Type *type = evalType(visitor, node->forStmt.var);

    AstNode *symbol = findSymbol(&ctx->env,
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
        if (range->tag == typPointer)
            range = stripPointer(range->pointer.pointed);

        if (range->tag == typArray) {
            const Type *elementType = range->array.elementType;
            node->forStmt.range = makeAstNode(
                ctx->pool,
                &node->forStmt.range->loc,
                &(AstNode){.tag = astUnaryExpr,
                           .type = range,
                           .flags = node->forStmt.range->flags,
                           .unaryExpr = {.op = opDeref,
                                         .operand = node->forStmt.range,
                                         .isPrefix = true}});
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

    popScope(&ctx->env);
}

void generateForStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *var = node->forStmt.var;
    const AstNode *range = node->forStmt.range;

    if (range->tag == astRangeExpr) {
        format(ctx->state, "for (", NULL);
        generateTypeUsage(ctx, var->type);
        format(ctx->state, " ", NULL);
        astConstVisit(visitor, var->varDecl.names);
        format(ctx->state, " = ", NULL);
        astConstVisit(visitor, range->rangeExpr.start);
        format(ctx->state, "; ", NULL);
        astConstVisit(visitor, var->varDecl.names);
        format(ctx->state, " < ", NULL);
        astConstVisit(visitor, range->rangeExpr.end);
        format(ctx->state, "; ", NULL);
        astConstVisit(visitor, var->varDecl.names);
        if (range->rangeExpr.step) {
            format(ctx->state, " += ", NULL);
            astConstVisit(visitor, range->rangeExpr.step);
        }
        else
            format(ctx->state, "++", NULL);
    }
    else if (range->type->tag == typArray) {
        cstring name = makeAnonymousVariable(ctx->strPool, "cyx_for");
        // create an array
        format(ctx->state, "{{{>}\n", NULL);
        if (range->tag == astArrayExpr)
            generateTypeUsage(ctx, range->type);
        else
            generateTypeUsage(
                ctx,
                &(const Type){
                    .tag = typPointer,
                    .flags = range->type->flags | node->forStmt.range->flags,
                    .pointer.pointed = range->type->array.elementType});

        format(ctx->state, " __arr_{s} = ", (FormatArg[]){{.s = name}});
        astConstVisit(visitor, range);
        format(ctx->state, ";\n", NULL);

        // create index variable
        format(ctx->state, "u64 __i_{s} = 0;\n", (FormatArg[]){{.s = name}});

        // Create actual loop variable
        generateTypeUsage(ctx, range->type->array.elementType);
        format(ctx->state, " ", NULL);
        astConstVisit(visitor, var->varDecl.names);
        format(ctx->state, " = __arr_{s}[0];\n", (FormatArg[]){{.s = name}});

        format(ctx->state,
               "for (; __i_{s} < {u64}; __i_{s}++, ",
               (FormatArg[]){
                   {.s = name}, {.u64 = range->type->array.size}, {.s = name}});
        astConstVisit(visitor, var->varDecl.names);
        format(ctx->state,
               " = __arr_{s}[__i_{s}]",
               (FormatArg[]){{.s = name}, {.s = name}});
    }
    else {
        unreachable("currently not supported");
    }

    format(ctx->state, ") ", NULL);
    astConstVisit(visitor, node->forStmt.body);

    if (range->type->tag == typArray) {
        format(ctx->state, "{<}\n}", NULL);
    }
}
