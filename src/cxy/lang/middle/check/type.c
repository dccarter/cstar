//
// Created by Carter Mbotho on 2024-01-09.
//

#include "check.h"

#include "../eval/eval.h"

#include "lang/frontend/flag.h"

#include "core/alloc.h"

static u64 addUnionDecl(UnionMember *members, const Type *member, u64 count)
{
    if (typeIs(member, Union)) {
        for (u64 i = 0; i < member->tUnion.count; i++) {
            count =
                addUnionDecl(members, member->tUnion.members[i].type, count);
        }
        return count;
    }

    for (u64 i = 0; i < count; i++) {
        if (members[i].type == member)
            return count;
    }
    members[count++] = (UnionMember){.type = member};
    return count;
}

const Type *checkTypeShallow(AstVisitor *visitor, AstNode *node, bool shallow)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    if (node == NULL)
        return NULL;

    ctx->shallow = shallow;
    if (hasFlag(node, Comptime)) {
        node->flags &= ~flgComptime;
        bool status = evaluate(ctx->evaluator, node);

        if (!status) {
            return node->type = ERROR_TYPE(ctx);
        }
    }

    if (node->type)
        return node->type;

    astVisit(visitor, node);
    const Type *type = resolveType(node->type),
               *unwrapped = unwrapType(type, NULL);
    if (typeIs(unwrapped, This) && unwrapped->_this.trackReferences) {
        pushThisReference(unwrapped, node);
    }
    return type;
}

void checkBuiltinType(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    switch (node->tag) {
    case astAutoType:
        node->type = makeAutoType(ctx->types);
        break;
    case astStringType:
        node->type = makeStringType(ctx->types);
        break;
    case astVoidType:
        node->type = makeVoidType(ctx->types);
        break;
    case astPrimitiveType:
        node->type = getPrimitiveType(ctx->types, node->primitiveType.id);
        break;
    default:
        csAssert0(false);
    }
}

void checkTypeDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    if (!hasFlag(node, ForwardDecl) && node->typeDecl.aliased) {
        const Type *type =
            makeThisType(ctx->types, node->typeDecl.name, flgNone);
        node->type = type;
        ((Type *)type)->_this.trackReferences = true;

        const Type *aliased = checkType(visitor, node->typeDecl.aliased);
        if (typeIs(aliased, Error)) {
            resolveThisReferences(ctx->types, type, NULL);
            node->type = aliased;
            return;
        }

        ((Type *)type)->_this.that = node->type = makeAliasType(
            ctx->types, aliased, node->typeDecl.name, node->flags & flgConst);
        resolveThisReferences(ctx->types, type, aliased);
    }
    else {
        node->type = makeOpaqueTypeWithFlags(
            ctx->types,
            node->typeDecl.name,
            getForwardDeclDefinition(node),
            hasFlag(node, ForwardDecl) ? flgForwardDecl : flgNone);
    }
}

void checkUnionDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *members = node->unionDecl.members, *member = members;
    u64 count = 0;

    for (; member; member = member->next) {
        const Type *type = checkType(visitor, member);
        if (typeIs(type, Error)) {
            node->type = ERROR_TYPE(ctx);
            continue;
        }
        if (typeIs(type, Union))
            count += type->tUnion.count;
        else
            count++;
    }

    if (typeIs(node->type, Error))
        return;

    UnionMember *members_ = mallocOrDie(sizeof(UnionMember) * count);
    u64 i = 0;
    member = node->unionDecl.members;
    for (; member; member = member->next) {
        i = addUnionDecl(members_, member->type, i);
    }

    node->type = makeUnionType(ctx->types, members_, i);
    free(members_);
}

void checkOptionalType(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *target = checkType(visitor, node->optionalType.type);
    if (target == NULL || typeIs(target, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    transformOptionalType(visitor, node, target);
}

void checkPointerType(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *pointed = checkType(visitor, node->pointerType.pointed);
    if (pointed == NULL || typeIs(pointed, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    node->type = makePointerType(ctx->types, pointed, node->flags & flgConst);
}
