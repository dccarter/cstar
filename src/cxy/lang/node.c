//
// Created by Carter on 2023-05-09.
//

#include "lang/node.h"

#include "lang/semantics.h"
#include "lang/ttable.h"

#include <string.h>

AstNode *makeAddressOf(SemanticsContext *ctx, AstNode *node)
{
    if (nodeIs(node, Path) || nodeIs(node, Identifier)) {
        return makeAstNode(
            ctx->pool,
            &node->loc,
            &(AstNode){.tag = astAddressOf,
                       .flags = node->flags,
                       .type = makePointerType(
                           ctx->typeTable, node->type, node->flags),
                       .unaryExpr = {
                           .isPrefix = true, .op = opAddrOf, .operand = node}});
    }
    else {
        cstring name = makeAnonymousVariable(ctx->strPool, "operand");
        AstNode *block = makeAstNode(ctx->pool,
                                     &node->loc,
                                     &(AstNode){
                                         .tag = astBlockStmt,
                                     });

        AstNode *next = block->blockStmt.stmts = makeAstNode(
            ctx->pool,
            &node->loc,
            &(AstNode){.tag = astVarDecl,
                       .type = node->type,
                       .flags = node->flags,
                       .varDecl = {.names = makeAstNode(
                                       ctx->pool,
                                       &node->loc,
                                       &(AstNode){.tag = astIdentifier,
                                                  .flags = node->flags,
                                                  .ident.value = name}),
                                   .init = node}});

        next = next->next = makeAstNode(
            ctx->pool,
            &node->loc,
            &(AstNode){
                .tag = astExprStmt,
                .type = next->type,
                .flags = next->flags,
                .exprStmt.expr = makeAstNode(
                    ctx->pool,
                    &node->loc,
                    &(AstNode){
                        .tag = astAddressOf,
                        .flags = node->flags,
                        .type = makePointerType(
                            ctx->typeTable, node->type, node->flags),
                        .unaryExpr = {.isPrefix = true,
                                      .op = opAddrOf,
                                      .operand = makeAstNode(
                                          ctx->pool,
                                          &node->loc,
                                          &(AstNode){.tag = astIdentifier,
                                                     .type = node->type,
                                                     .flags = node->flags,
                                                     .ident.value = name})}})});

        return makeAstNode(ctx->pool,
                           &node->loc,
                           &(AstNode){.tag = astStmtExpr,
                                      .type = next->type,
                                      .flags = node->flags,
                                      .stmtExpr.stmt = block});
    }
}

void transformToMemberCallExpr(AstVisitor *visitor,
                               AstNode *node,
                               AstNode *func,
                               AstNode *target,
                               cstring member,
                               AstNode *args)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *funcMember = NULL;
    if (typeIs(func->type, Generic) && args) {
        // infer the argument
        const Type *arg = evalType(visitor, args);
        funcMember = makeAstNode(
            ctx->pool,
            &target->loc,
            &(AstNode){
                .tag = astPathElem,
                .flags = args->flags,
                .pathElement = {.name = member,
                                .args = makeTypeReferenceNode(ctx, arg)}});
    }
    else {
        funcMember =
            makeAstNode(ctx->pool,
                        &target->loc,
                        &(AstNode){.tag = astPathElem,
                                   .flags = args ? args->flags : flgNone,
                                   .pathElement = {.name = member}});
    }

    AstNode *path = makeAstNode(ctx->pool,
                                &target->loc,
                                &(AstNode){.tag = astPath,
                                           .flags = target->flags,
                                           .type = target->type,
                                           .path = {.elements = funcMember}});

    AstNode *callee = makeAstNode(
        ctx->pool,
        &target->loc,
        &(AstNode){.tag = astMemberExpr,
                   .flags = target->flags,
                   .type = target->type,
                   .memberExpr = {.target = target, .member = path}});

    memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);
    node->tag = astCallExpr;
    node->type = NULL;
    node->callExpr.callee = callee;
    node->callExpr.args = args;
}

bool transformToTruthyOperator(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *type = node->type ?: evalType(visitor, node);
    if (!typeIs(type, Struct))
        return false;

    AstNode *symbol = findSymbolOnly(type->tStruct.env, "op_truthy");
    if (symbol == NULL)
        return false;

    transformToMemberCallExpr(visitor,
                              node,
                              symbol,
                              cloneAstNode(ctx->pool, node),
                              "op_truthy",
                              NULL);

    type = evalType(visitor, node);
    return typeIs(type, Primitive);
}

bool transformToDerefOperator(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *type = node->type ?: evalType(visitor, node->unaryExpr.operand);
    if (!typeIs(type, Struct))
        return false;

    AstNode *symbol = findSymbolOnly(type->tStruct.env, "op_deref");
    if (symbol == NULL)
        return false;

    transformToMemberCallExpr(
        visitor, node, symbol, node->unaryExpr.operand, "op_deref", NULL);

    type = evalType(visitor, node);
    return !typeIs(type, Error);
}
