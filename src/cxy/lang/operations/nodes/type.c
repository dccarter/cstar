//
// Created by Carter on 2023-08-30.
//

#include "../check.h"
#include "../codegen.h"

#include "lang/flag.h"
#include "lang/ttable.h"

#include "core/alloc.h"

static AstNode *resolveAliasedNode(AstNode *node)
{
    AstNode *aliased = node->typeDecl.aliased;
    if (nodeIs(aliased, Path)) {
        AstNode *element = aliased->path.elements;
        do {
            aliased = element->pathElement.resolvesTo;
            element = element->next;
        } while (element);
    }

    return aliased;
}

static bool comptimeCompareManyTypes(const AstNode *lhs, const AstNode *rhs)
{
    while (lhs && rhs) {
        if (!comptimeCompareTypes(lhs, rhs))
            return false;
        lhs = lhs->next;
        rhs = rhs->next;
    }

    return lhs == NULL && rhs == NULL;
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

void generateTypeDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (!(node->flags & flgNative))
        generateTypeUsage(ctx, node->type);
}

void generateTypeinfo(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    csAssert0((hasFlag(node, Typeinfo)));
    writeTypename(ctx, node->type->info.target);
}

void checkTypeDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *aliased = checkType(visitor, node->typeDecl.aliased);
    if (typeIs(aliased, Error)) {
        node->type = aliased;
        return;
    }

    node->type = makeAliasType(
        ctx->types, aliased, node->typeDecl.name, node->flags & flgConst);
}

void checkUnionDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *members = node->unionDecl.members, *member = members;
    u64 count = node->tupleType.len;
    const Type **members_ = mallocOrDie(sizeof(Type *) * count);

    for (u64 i = 0; member; member = member->next) {
        members_[i] = checkType(visitor, member);
        if (typeIs(members_[i], Error))
            node->type = ERROR_TYPE(ctx);
    }

    if (typeIs(node->type, Error)) {
        free(members_);
        return;
    }

    node->type = makeUnionType(ctx->types, members_, count);

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

    node->type = makeOptionalType(ctx->types, target, node->flags & flgConst);
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

bool comptimeCompareTypes(const AstNode *lhs, const AstNode *rhs)
{
    if (lhs->tag != rhs->tag)
        return lhs->tag == astAutoType;

    switch (lhs->tag) {
    case astStringType:
    case astVoidType:
        return true;
    case astPrimitiveType:
        return lhs->primitiveType.id == rhs->primitiveType.id;
    case astArrayType:
        return lhs->arrayType.dim == rhs->arrayType.dim &&
               comptimeCompareTypes(lhs->arrayType.elementType,
                                    rhs->arrayType.elementType);
    case astTupleType:
        return comptimeCompareManyTypes(lhs->tupleType.elements,
                                        rhs->tupleType.elements);
    case astPointerType:
        return comptimeCompareTypes(lhs->pointerType.pointed,
                                    rhs->pointerType.pointed);
    case astOptionalType:
        return comptimeCompareTypes(lhs->optionalType.type,
                                    rhs->optionalType.type);
    case astFuncType:
        return comptimeCompareTypes(lhs->funcType.ret, rhs->funcType.params);
    case astEnumDecl:
        return lhs->enumDecl.name == rhs->enumDecl.name;
    case astStructDecl:
        return lhs->structDecl.name == rhs->structDecl.name;
    case astFuncDecl:
        return lhs->funcDecl.name == rhs->funcDecl.name;
    default:
        return false;
    }
}
