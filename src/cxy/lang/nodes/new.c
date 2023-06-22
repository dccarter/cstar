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
            .flags = callee->flags | flgImmediatelyReturned,
            .varDecl = {.names = makeAstNode(ctx->pool,
                                             &callee->loc,
                                             &(AstNode){.tag = astIdentifier,
                                                        .ident.value = name}),
                        .init = newExpr}});

    // tmp.init
    AstNode *newCallee = makeAstNode(
        ctx->pool,
        &callee->loc,
        &(AstNode){.tag = astPath,
                   .flags = callee->flags,
                   .path.elements = makeAstNode(
                       ctx->pool,
                       &node->loc,
                       &(AstNode){.tag = astPathElem,
                                  .flags = callee->flags,
                                  .pathElement.name = name,
                                  .next = makeAstNode(
                                      ctx->pool,
                                      &node->loc,
                                      &(AstNode){
                                          .tag = astPathElem,
                                          .flags = callee->flags,
                                          .pathElement.name = makeString(
                                              ctx->strPool, "op_new"),
                                      })})});

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
            AstNode *calleeNode = init->callExpr.callee;
            if (nodeIs(calleeNode, Path) && hasFlag(callee, Generated)) {
                calleeNode->path.elements->pathElement.name =
                    calleeNode->path.elements->pathElement.alt2;
            }
            return checkNewInitializerOverload(visitor, node);
        }
        else {
            if (init->callExpr.args == NULL) {
                node->newExpr.init = NULL;
                return callee;
            }
            if (init->callExpr.args->next) {
                logError(ctx->L,
                         &init->callExpr.args->next->loc,
                         "`new` initializer expression for type '{t}' accepts "
                         "only 1 parameter",
                         (FormatArg[]){{.t = callee}});
                return NULL;
            }
            node->newExpr.init = init->callExpr.args;
            const Type *type = evalType(visitor, node->newExpr.init);
            if (typeIs(type, Error))
                return NULL;
            if (!isExplicitConstructibleFrom(ctx, callee, type)) {
                logError(
                    ctx->L,
                    &init->callExpr.args->loc,
                    "type '{t}' cannot be constructed with value of type '{t}'",
                    (FormatArg[]){{.t = callee}, {.t = type}});
                return NULL;
            }
            return callee;
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
    format(
        ctx->state, " {s} = cxy_calloc(1, sizeof(", (FormatArg[]){{.s = name}});
    generateTypeUsage(ctx, type);
    format(ctx->state, "),\n", NULL);
    if (typeIs(type, Struct) || typeIs(type, Array) || typeIs(type, Tuple)) {
        writeTypename(ctx, type);
        format(ctx->state, "__builtin_destructor", NULL);
    }
    else
        format(ctx->state, "nullptr", NULL);

    format(ctx->state, ");\n", NULL);
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
    u64 flags = flgNewAllocated;

    if (node->newExpr.type) {
        flags = node->newExpr.type->flags;
        type = evalType(visitor, node->newExpr.type);
    }

    if (node->newExpr.init) {
        if (flags == flgNone)
            flags = node->newExpr.init->flags;

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
        node->type = ERROR_TYPE(ctx);
        return;
    }

    node->flags = flags;
    node->type =
        makePointerType(ctx->typeTable, type, type->flags | flgNewAllocated);
}
