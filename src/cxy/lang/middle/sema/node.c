//
// Created by Carter on 2023-05-09.
//

#include "check.h"

#include <lang/middle/builtins.h>

#include <lang/frontend/flag.h>
#include <lang/frontend/strings.h>

#include <string.h>

static AstNode *makeStructInitializerForDefaults(TypingContext *ctx,
                                                 AstNode *node)
{
    AstNode *decl = typeIs(node->type, Class) ? node->type->tClass.decl
                                              : node->type->tStruct.decl;
    AstNode *member = decl ? decl->structDecl.members : NULL;
    AstNodeList init = {};
    for (; member; member = member->next) {
        if (!nodeIs(member, FieldDecl) || hasFlag(member, Static)) {
            continue;
        }
        AstNode *value =
            member->structField.value
                ? deepCloneAstNode(ctx->pool, member->structField.value)
                : makeDefaultValue(ctx->pool, member->type, &node->loc);
        insertAstNode(&init,
                      makeFieldExpr(ctx->pool,
                                    &member->loc,
                                    member->structField.name,
                                    value->flags,
                                    value,
                                    NULL));
    }
    return init.first;
}

static AstNode *transformRValueToLValue(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    if (node == NULL || !nodeIs(node, CallExpr))
        return node;

    cstring tmpSymbol = makeAnonymousVariable(ctx->strings, "_rv");
    AstNode *tmpVar = makeVarDecl(ctx->pool,
                                  &node->loc,
                                  node->flags & flgConst,
                                  tmpSymbol,
                                  NULL,
                                  node,
                                  NULL,
                                  node->type);
    astVisit(visitor, node);
    addBlockLevelDeclaration(ctx, tmpVar);

    return makeResolvedPath(ctx->pool,
                            &node->loc,
                            tmpSymbol,
                            tmpVar->flags,
                            tmpVar,
                            NULL,
                            tmpVar->type);
}

void transformToMemberCallExpr(AstVisitor *visitor,
                               AstNode *node,
                               AstNode *target,
                               cstring member,
                               AstNode *args)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *callee = makeMemberExpr(
        ctx->pool,
        &target->loc,
        target->flags,
        target,
        makeIdentifier(ctx->pool, &target->loc, member, 0, NULL, NULL),
        NULL,
        NULL);
    callee->parentScope = node;
    clearAstBody(node);
    node->tag = astCallExpr;
    node->type = NULL;
    node->callExpr.callee = callee;
    node->callExpr.args = transformRValueToLValue(visitor, args);
}

const Type *transformToConstructCallExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *callee = node->callExpr.callee;
    u64 flags = (callee->flags & ~flgTopLevelDecl);
    const Type *target = resolveAndUnThisType(callee->type);
    // find constructor
    AstNode *ctor = isClassType(target) ? findBuiltinDecl(S___construct0)
                                        : findBuiltinDecl(S___construct1);
    csAssert0(ctor);
    // __construct[T]
    node->callExpr.callee = makeResolvedPathWithArgs(
        ctx->pool,
        &callee->loc,
        getDeclarationName(ctor),
        flags,
        ctor,
        makeTypeReferenceNode(ctx->pool, target, &callee->loc),
        NULL);
    node->type = NULL;

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
        visitor,
        node,
        transformRValueToLValue(visitor, shallowCloneAstNode(ctx->pool, node)),
        S_Truthy,
        NULL);

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
    AstNode *optional = (AstNode *)findBuiltinDecl(S___Optional);
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

AstNode *transformToUnionValue(TypingContext *ctx,
                               AstNode *right,
                               const Type *lhs,
                               const Type *rhs)
{
    const Type *stripped = stripOnce(lhs, NULL);
    if (rhs != lhs && typeIs(stripped, Union) && stripped != rhs) {
        u32 idx = findUnionTypeIndex(stripped, rhs);
        csAssert0(idx != UINT32_MAX);
        return makeUnionValueExpr(
            ctx->pool, &right->loc, right->flags, right, idx, NULL, lhs);
    }
    return right;
}
