//
// Created by Carter Mbotho on 2024-03-23.
//

#include "check.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"

AstNode *vTableMembersFind(TypingContext *ctx,
                           AstNode *members,
                           const AstNode *member)
{
    AstNode *it = members;
    while (it) {
        if (member->_namedNode.name == it->_namedNode.name) {
            return it;
        }
        it = it->next;
    }
    return NULL;
}

static AstNode *makeVTableVariable(TypingContext *ctx,
                                   AstNode *node,
                                   AstNode *vTableType,
                                   AstNode *init)
{
    AstNode *vTable = makeVarDecl(
        ctx->pool,
        &node->loc,
        flgConst | flgTopLevelDecl | (node->flags & flgPublic),
        makeStringConcat(ctx->strings, getDeclarationName(node), "_vTable"),
        NULL,
        makeStructExpr(ctx->pool,
                       &node->loc,
                       flgConst,
                       makeResolvedIdentifier(ctx->pool,
                                              &node->loc,
                                              vTableType->_namedNode.name,
                                              0,
                                              vTableType,
                                              NULL,
                                              vTableType->type),
                       init,
                       NULL,
                       vTableType->type),
        NULL,
        vTableType->type);
    astModifierAdd(&ctx->root, vTable);
    return vTable;
}

static AstNode *makeVTableMemberInit(TypingContext *ctx,
                                     AstNode *node,
                                     AstNode *vTable)
{
    const Type *type_ = makePointerType(ctx->types, vTable->type, flgNone);
    return makePointerOfExpr(ctx->pool,
                             &node->loc,
                             flgConst,
                             makeResolvedIdentifier(ctx->pool,
                                                    &node->loc,
                                                    vTable->_namedNode.name,
                                                    0,
                                                    vTable,
                                                    NULL,
                                                    vTable->type),
                             NULL,
                             type_);
}

static AstNode *makeVTableMember(TypingContext *ctx,
                                 AstNode *node,
                                 AstNode *vTable)
{
    const Type *type_ = makePointerType(ctx->types, vTable->type, flgNone);
    return makeStructField(ctx->pool,
                           &node->loc,
                           S_vtable,
                           flgConst | flgVTable,
                           makeTypeReferenceNode(ctx->pool, type_, &node->loc),
                           makeVTableMemberInit(ctx, node, vTable),
                           NULL);
}

static inline AstNode *getVTableMember(AstNode *node)
{
    csAssert0(hasFlag(node, Virtual));
    return node->classDecl.members;
}

static AstNode *getVTableDefault(AstNode *node)
{
    AstNode *member = getVTableMember(node);
    csAssert0(nodeIs(member, FieldDecl));
    AstNode *value = member->structField.value;
    csAssert0(nodeIs(value, PointerOf));
    value = value->unaryExpr.operand;
    csAssert0(nodeIs(value, Identifier));
    value = value->ident.resolvesTo;
    csAssert0(nodeIs(value, VarDecl));
    return value->varDecl.init;
}

static AstNode *getVTableType(AstNode *node)
{
    AstNode *def = getVTableDefault(node);
    csAssert0(nodeIs(def, StructExpr));
    AstNode *type = def->structExpr.left;
    csAssert0(nodeIs(type, Identifier));
    return type->ident.resolvesTo;
}

static AstNode *makeMemberFunctionRef(TypingContext *ctx,
                                      AstNode *node,
                                      AstNode *member)
{
    return makePathWithElements(
        ctx->pool,
        &member->loc,
        flgConst,
        makeResolvedPathElement(
            ctx->pool,
            &member->loc,
            getDeclarationName(node),
            flgConst,
            node,
            makeResolvedPathElement(ctx->pool,
                                    &member->loc,
                                    getDeclarationName(member),
                                    flgConst,
                                    member,
                                    NULL,
                                    member->type),
            member->type),
        NULL);
}

static void inheritanceChainBuildVTableType(TypingContext *ctx,
                                            AstNodeList *members,
                                            AstNodeList *init,
                                            AstNode *node)
{
    AstNode *base = node->classDecl.base;
    if (base != NULL) {
        inheritanceChainBuildVTableType(ctx, members, init, resolvePath(base));
        AstNode *field = init->first;
        for (; field; field = field->next) {
            AstNode *member =
                findMemberDeclInType(node->type, field->_namedNode.name);
            if (nodeIs(member, FuncDecl)) {
                field->fieldExpr.value =
                    makeMemberFunctionRef(ctx, node, member);
            }
        }
    }

    if (!hasFlag(node, Virtual))
        return;

    AstNode *member = node->classDecl.members;
    for (; member; member = member->next) {
        if (!hasFlag(member, Virtual))
            continue;
        AstNode *field = vTableMembersFind(ctx, members->first, member);
        if (field)
            continue;
        insertAstNode(
            members,
            makeStructField(
                ctx->pool,
                &member->loc,
                member->_namedNode.name,
                flgConst,
                makeTypeReferenceNode(ctx->pool, member->type, &member->loc),
                NULL,
                NULL));

        insertAstNode(
            init,
            makeFieldExpr(
                ctx->pool,
                &member->loc,
                member->_namedNode.name,
                flgConst,
                member->funcDecl.body
                    ? makeMemberFunctionRef(ctx, node, member)
                    : makeNullLiteral(
                          ctx->pool, &member->loc, NULL, member->type),
                member,
                NULL));
    }
}

AstNode *inheritanceBuildVTableType(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNodeList members = {NULL}, init = {NULL};
    csAssert0(hasFlag(node, Virtual));
    inheritanceChainBuildVTableType(ctx, &members, &init, node);
    csAssert0(members.first != NULL);

    AstNode *vTableType = makeStructDecl(
        ctx->pool,
        &node->loc,
        flgNone,
        makeStringConcat(ctx->strings, getDeclarationName(node), "_VTable"),
        members.first,
        NULL,
        NULL);

    node->type = checkType(visitor, vTableType);
    if (typeIs(node->type, Error))
        return NULL;
    astModifierAdd(&ctx->root, vTableType);

    AstNode *vTable = makeVTableVariable(ctx, node, vTableType, init.first);
    AstNode *member = makeVTableMember(ctx, node, vTable);
    member->next = node->classDecl.members;
    node->classDecl.members = member;
    return member;
}

AstNode *inheritanceMakeVTableMember(TypingContext *ctx,
                                     AstNode *node,
                                     AstNode *init)
{
    AstNode *type = getVTableType(resolvePath(node->classDecl.base));
    const Type *type_ = makePointerType(ctx->types, type->type, flgNone);
    AstNode *member =
        makeStructField(ctx->pool,
                        &node->loc,
                        S_vtable,
                        flgConst | flgVTable,
                        makeTypeReferenceNode(ctx->pool, type_, &node->loc),
                        init,
                        NULL);
    member->next = node->classDecl.members;
    node->classDecl.members = member;
    return member;
}

AstNode *inheritanceBuildVTable(TypingContext *ctx, AstNode *node)
{
    AstNode *base = resolvePath(node->classDecl.base);
    AstNode *def = getVTableDefault(base), *type = getVTableType(base);

    AstNodeList value = {NULL};
    AstNode *member = def->structExpr.fields;
    for (; member; member = member->next) {
        AstNode *func =
            findMemberDeclInType(node->type, member->_namedNode.name);
        if (func != NULL) {
            // Change the function we resolve to
            insertAstNode(
                &value,
                makeFieldExpr(
                    ctx->pool,
                    &member->loc,
                    member->_namedNode.name,
                    member->flags,
                    func->funcDecl.body
                        ? makeMemberFunctionRef(ctx, node, func)
                        : makeNullLiteral(
                              ctx->pool, &func->loc, NULL, func->type),
                    member,
                    NULL));
        }
        else {
            insertAstNode(&value, deepCloneAstNode(ctx->pool, member));
        }
    }

    AstNode *vTable = makeVTableVariable(ctx, node, type, value.first);
    return makeVTableMemberInit(ctx, node, vTable);
}
