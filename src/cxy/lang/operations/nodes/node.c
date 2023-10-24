//
// Created by Carter on 2023-05-09.
//

#include "../check.h"

#include "lang/builtins.h"
#include "lang/flag.h"
#include "lang/scope.h"
#include "lang/strings.h"
#include "lang/ttable.h"

#include <strings.h>

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

const Type *transformToConstructCallExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *callee = node->callExpr.callee;
    u64 flags = (callee->flags & ~flgTopLevelDecl);

    // turn S(...) => ({ var tmp = S{}; tmp.init(...); tmp; })

    cstring name = makeAnonymousVariable(ctx->strings, "_new_tmp");
    // S{}
    AstNode *structExpr = typeIs(callee->type, Struct)
                              ? makeStructExpr(ctx->pool,
                                               &callee->loc,
                                               flags,
                                               callee,
                                               NULL,
                                               NULL,
                                               callee->type)
                              : makeNewClassCall(ctx, callee);
    // var name = S{}
    AstNode *varDecl = makeVarDecl(ctx->pool,
                                   &callee->loc,
                                   flags | flgImmediatelyReturned,
                                   name,
                                   NULL,
                                   structExpr,
                                   NULL,
                                   NULL);
    // tmp.init
    AstNode *newCallee = makePathWithElements(
        ctx->pool,
        &callee->loc,
        flags,
        makeResolvedPathElement(
            ctx->pool,
            &callee->loc,
            name,
            flags,
            varDecl,
            makePathElement(
                ctx->pool, &callee->loc, S_InitOverload, flags, NULL, NULL),
            NULL),
        NULL);

    // tmp;
    AstNode *ret = makeExprStmt(
        ctx->pool,
        &node->loc,
        node->flags,
        makeResolvedPath(
            ctx->pool, &node->loc, name, flgNone, varDecl, NULL, NULL),
        NULL,
        NULL);

    //     name.init(...); tmp
    varDecl->next = makeCallExpr(ctx->pool,
                                 &node->loc,
                                 newCallee,
                                 node->callExpr.args,
                                 node->flags,
                                 ret,
                                 NULL);

    memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);
    clearAstBody(node);
    node->tag = astStmtExpr;
    node->stmtExpr.stmt =
        makeBlockStmt(ctx->pool, &node->loc, varDecl, NULL, NULL);
    node->stmtExpr.stmt->flags = flgBlockReturns;

    return checkType(visitor, node);
}

bool transformToTruthyOperator(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *type = node->type ?: checkType(visitor, node);
    type = unwrapType(type, NULL);
    if (!isClassOrStructType(type))
        return false;

    const Type *member = findMemberInType(type, S_Truthy);
    if (member == NULL)
        return false;

    transformToMemberCallExpr(
        visitor, node, shallowCloneAstNode(ctx->pool, node), S_Truthy, NULL);

    type = checkType(visitor, node);
    return typeIs(type, Primitive);
}

bool transformToAwaitOperator(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *operand = node->unaryExpr.operand;
    const Type *type = operand->type ?: checkType(visitor, operand);
    type = unwrapType(type, NULL);
    if (!isClassOrStructType(type))
        return false;

    const Type *member = findMemberInType(type, S_Await);
    if (member == NULL)
        return false;

    transformToMemberCallExpr(visitor, node, operand, S_Await, NULL);

    type = checkType(visitor, node);
    return !(typeIs(type, Error));
}

bool transformOptionalSome(AstVisitor *visitor, AstNode *node, AstNode *value)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *some = (AstNode *)findBuiltinDecl(S_Some);
    csAssert0(some);

    const Type *type = checkType(visitor, value);
    if (typeIs(type, Error))
        return false;

    clearAstBody(node);
    node->tag = astCallExpr;
    node->flags = flgNone;
    node->type = NULL;
    node->callExpr.callee = makeResolvedPath(
        ctx->pool, &node->loc, S_Some, flgNone, some, NULL, NULL);
    node->callExpr.args = value;

    type = checkType(visitor, node);

    return !typeIs(type, Error);
}

bool transformOptionalNone(AstVisitor *visitor, AstNode *node, const Type *type)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *none = (AstNode *)findBuiltinDecl(S_None);
    csAssert0(none);

    clearAstBody(node);
    node->tag = astCallExpr;
    node->flags = flgNone;
    node->type = NULL;

    node->callExpr.callee = makeResolvedPathWithArgs(
        ctx->pool,
        &node->loc,
        S_None,
        flgNone,
        none,
        makeTypeReferenceNode(ctx->pool, type, &node->loc),
        NULL);
    type = checkType(visitor, node);

    return !typeIs(type, Error);
}

bool transformOptionalType(AstVisitor *visitor, AstNode *node, const Type *type)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *optional = (AstNode *)findBuiltinDecl(S_Optional);
    csAssert0(optional);

    clearAstBody(node);
    node->tag = astPath;
    node->flags = flgNone;
    node->path.elements = makeResolvedPathElementWithArgs(
        ctx->pool,
        &node->loc,
        S_None,
        flgNone,
        optional,
        NULL,
        makeTypeReferenceNode(ctx->pool, type, &node->loc),
        NULL);
    type = checkType(visitor, node);

    return !typeIs(type, Error);
}

bool transformToDerefOperator(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *type =
        node->type ?: checkType(visitor, node->unaryExpr.operand);
    if (!isClassOrStructType(type))
        return false;

    type = stripAll(type);
    if (findMemberInType(type, S_Deref) == NULL)
        return false;

    transformToMemberCallExpr(
        visitor, node, node->unaryExpr.operand, S_Deref, NULL);

    type = checkType(visitor, node);
    return !typeIs(type, Error);
}
