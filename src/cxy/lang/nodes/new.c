//
// Created by Carter on 2023-04-18.
//

#include "lang/codegen.h"
#include "lang/semantics.h"

#include "lang/ttable.h"

#include <memory.h>

static inline bool isSupportedOnAuto(AstNode *node)
{
    switch (node->tag) {
    case astIntegerLit:
    case astFloatLit:
    case astCharLit:
    case astBoolLit:
    case astStringLit:
    case astNullLit:
    case astTupleExpr:
    case astArrayExpr:
    case astStructExpr:
        return true;
    default:
        return false;
    }
}

static const Type *checkNewInitializerOverload(AstVisitor *visitor,
                                               AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *init = node->newExpr.init;
    AstNode *callee = init->callExpr.callee;
    // turn new S(...) => ({ var tmp = new S{}; tmp.init(...); })

    cstring name = makeAnonymousVariable(ctx->strPool, "_new_tmp");
    // new S{}
    AstNode *newExpr = makeAstNode(ctx->pool,
                                   &callee->loc,
                                   &(AstNode){.tag = astNewExpr,
                                              .flags = callee->flags,
                                              .newExpr = {.type = callee}});
    // var name = new S{}
    AstNode *varDecl = makeAstNode(
        ctx->pool,
        &callee->loc,
        &(AstNode){
            .tag = astVarDecl,
            .flags = callee->flags,
            .varDecl = {.names = makeAstNode(ctx->pool,
                                             &callee->loc,
                                             &(AstNode){.tag = astIdentifier,
                                                        .ident.value = name}),
                        .init = newExpr}});

    // tmp.init
    AstNode *newCallee = makeAstNode(
        ctx->pool,
        &callee->loc,
        &(AstNode){
            .tag = astMemberExpr,
            .flags = callee->flags,
            .memberExpr = {
                .target = makeAstNode(ctx->pool,
                                      &init->loc,
                                      &(AstNode){.tag = astIdentifier,
                                                 .flags = callee->flags,
                                                 .ident.value = name}),
                .member = makeAstNode(ctx->pool,
                                      &init->loc,
                                      &(AstNode){.tag = astIdentifier,
                                                 .flags = callee->flags,
                                                 .ident.value = "op_new"})}});

    AstNode *ret =
        makeAstNode(ctx->pool,
                    &init->loc,
                    &(AstNode){.tag = astExprStmt,
                               .flags = init->flags,
                               .exprStmt.expr = makeAstNode(
                                   ctx->pool,
                                   &init->loc,
                                   &(AstNode){.tag = astIdentifier,
                                              .flags = init->flags,
                                              .ident.value = name})});

    //     name.init
    varDecl->next =
        makeAstNode(ctx->pool,
                    &init->loc,
                    &(AstNode){.tag = astCallExpr,
                               .flags = init->flags,
                               .callExpr = {.callee = newCallee,
                                            .args = init->callExpr.args},
                               .next = ret});

    AstNode *block = makeAstNode(
        ctx->pool,
        &init->loc,
        &(AstNode){.tag = astBlockStmt, .blockStmt.stmts = varDecl});

    memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);
    node->tag = astStmtExpr;
    node->stmtExpr.stmt = block;

    return evalType(visitor, node);
}

static const Type *checkNewInitializerExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *init = node->newExpr.init;

    if (isSupportedOnAuto(init))
        return evalType(visitor, init);

    if (nodeIs(init, CallExpr)) {
        if (!hasFlag(init->callExpr.callee, TypeAst)) {
            logError(ctx->L,
                     &init->callExpr.callee->loc,
                     "only types can be created using the new expression",
                     NULL);

            return NULL;
        }

        const Type *callee = evalType(visitor, init->callExpr.callee);
        if (typeIs(callee, Struct)) {
            if (!findSymbolOnly(callee->tStruct.env, "op_new")) {
                logError(ctx->L,
                         &init->callExpr.callee->loc,
                         "cannot use `new` constructor expression on type "
                         "'{t}', structure does not overload new operator",
                         (FormatArg[]){{.t = callee}});
                return NULL;
            }

            return checkNewInitializerOverload(visitor, node);
        }
    }

    return NULL;
}

void generateNewExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const char *name = makeAnonymousVariable((ctx)->strPool, "cxy_new_temp");
    const Type *type = node->type->pointer.pointed;

    format(ctx->state, "({{{>}\n", NULL);
    generateTypeUsage(ctx, node->type);
    format(ctx->state, " {s} = cxy_alloc(sizeof(", (FormatArg[]){{.s = name}});
    generateTypeUsage(ctx, type);
    format(ctx->state, "));\n", NULL);
    if (node->newExpr.init) {
        if (type->tag == typArray) {
            format(ctx->state, " memcpy(*{s}, &(", (FormatArg[]){{.s = name}});
            generateTypeUsage(ctx, type);
            format(ctx->state, ")", NULL);
            astConstVisit(visitor, node->newExpr.init);
            format(ctx->state, ", sizeof(", NULL);
            generateTypeUsage(ctx, type);
            format(ctx->state, "))", NULL);
        }
        else {
            format(ctx->state, " *{s} = ", (FormatArg[]){{.s = name}});
            astConstVisit(visitor, node->newExpr.init);
        }
        format(ctx->state, ";\n", NULL);
    }
    format(ctx->state, " {s};{<}\n})", (FormatArg[]){{.s = name}});
}

void checkNewExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *type = NULL, *init = NULL;
    node->flags |= flgNewAllocated;

    if (node->newExpr.type)
        type = evalType(visitor, node->newExpr.type);

    if (node->newExpr.init) {
        init = checkNewInitializerExpr(visitor, node);
        if (init == NULL) {
            logError(ctx->L,
                     &node->loc,
                     "`new` operator syntax not supported",
                     NULL);
            node->type = ERROR_TYPE(ctx);
            return;
        }
        if (nodeIs(node, StmtExpr))
            return;
    }

    if (type == NULL) {
        type = init;
    }

    if (init && !isTypeAssignableFrom(type, init)) {
        logError(
            ctx->L,
            &node->loc,
            "new initializer value type '{t}' is not assignable to type '{t}'",
            (FormatArg[]){{.t = type}, {.t = init}});
    }
    node->flags = (node->newExpr.type ? node->newExpr.type->flags
                                      : node->newExpr.init->flags);
    node->type =
        makePointerType(ctx->typeTable, type, type->flags | flgNewAllocated);
}