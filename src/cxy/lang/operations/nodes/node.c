//
// Created by Carter on 2023-05-09.
//

#include "../check.h"

#include "lang/flag.h"
#include "lang/strings.h"
#include "lang/ttable.h"

#include <string.h>

AstNode *makeAddressOf(TypingContext *ctx, AstNode *node)
{
    if (nodeIs(node, Path) || nodeIs(node, Identifier)) {
        return makeAstNode(
            ctx->pool,
            &node->loc,
            &(AstNode){.tag = astAddressOf,
                       .flags = node->flags,
                       .type =
                           makePointerType(ctx->types, node->type, node->flags),
                       .unaryExpr = {
                           .isPrefix = true, .op = opAddrOf, .operand = node}});
    }
    else {
        cstring name = makeAnonymousVariable(ctx->strings, "operand");
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
                .type = makePointerType(ctx->types, node->type, node->flags),
                .flags = next->flags,
                .exprStmt.expr = makeAstNode(
                    ctx->pool,
                    &node->loc,
                    &(AstNode){
                        .tag = astAddressOf,
                        .flags = node->flags,
                        .type = makePointerType(
                            ctx->types, node->type, node->flags),
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
                               AstNode *target,
                               cstring member,
                               AstNode *args)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *funcMember = makeAstNode(
        ctx->pool,
        &target->loc,
        &(AstNode){.tag = astPathElem,
                   .flags = args ? (args->flags & ~flgAddThis) : flgNone,
                   .pathElement = {.name = member}});

    AstNode *path = makeAstNode(ctx->pool,
                                &target->loc,
                                &(AstNode){.tag = astPath,
                                           .flags = target->flags,
                                           .type = NULL,
                                           .path = {.elements = funcMember}});

    AstNode *callee = makeAstNode(
        ctx->pool,
        &target->loc,
        &(AstNode){.tag = astMemberExpr,
                   .flags = target->flags,
                   .type = NULL,
                   .memberExpr = {.target = target, .member = path}});

    clearAstBody(node);
    node->tag = astCallExpr;
    node->type = NULL;
    node->callExpr.callee = callee;
    node->callExpr.args = args;
}

bool transformToTruthyOperator(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *type = node->type ?: checkType(visitor, node);
    if (!typeIs(type, Struct))
        return false;

    const StructMember *member = findStructMember(type, S_Truthy);
    if (member == NULL)
        return false;

    transformToMemberCallExpr(
        visitor, node, shallowCloneAstNode(ctx->pool, node), S_Truthy, NULL);

    type = checkType(visitor, node);
    return typeIs(type, Primitive);
}

bool transformToDerefOperator(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *type =
        node->type ?: checkType(visitor, node->unaryExpr.operand);
    if (!typeIs(type, Struct))
        return false;

    const StructMember *member = findStructMember(type, S_Deref);
    if (member == NULL)
        return false;

    transformToMemberCallExpr(
        visitor, node, node->unaryExpr.operand, S_Deref, NULL);

    type = checkType(visitor, node);
    return !typeIs(type, Error);
}
