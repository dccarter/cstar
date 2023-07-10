//
// Created by Carter on 2023-04-18.
//

#include "lang/codegen.h"
#include "lang/semantics.h"

#include "lang/flag.h"
#include "lang/ttable.h"
#include "lang/visitor.h"

#include "core/alloc.h"

static AstNode *resolveAliasedNode(SemanticsContext *ctx, AstNode *node)
{
    AstNode *aliased = node->typeDecl.aliased;
    if (nodeIs(aliased, Path) || nodeIs(aliased, Identifier)) {
        aliased = findSymbolByNode(ctx->env, NULL, aliased);
    }
    return aliased;
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
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    if (node->typeDecl.aliased) {
        const Type *ref = evalType(visitor, node->typeDecl.aliased);
        if (typeIs(ref, Error)) {
            node->type = ref;
            return;
        }

        AstNode *aliased = resolveAliasedNode(ctx, node);
        if (!defineDeclaration(ctx,
                               node->typeDecl.name,
                               getDeclarationAlias(ctx, node),
                               aliased,
                               hasFlag(node, Public))) //
        {
            node->type = ERROR_TYPE(ctx);
            return;
        }

        node->type = makeAliasType(
            ctx->typeTable, ref, node->typeDecl.name, node->flags & flgNative);
    }
    else {
        if (!defineDeclaration(ctx,
                               node->typeDecl.name,
                               getDeclarationAlias(ctx, node),
                               node,
                               hasFlag(node, Public))) //
        {
            node->type = ERROR_TYPE(ctx);
            return;
        }

        node->type = makeOpaqueType(ctx->typeTable, node->typeDecl.name);
    }
}

void checkUnionDecl(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    u64 count = countAstNodes(node->unionDecl.members);
    const Type **members = mallocOrDie(sizeof(Type *) * count);

    AstNode *member = node->unionDecl.members;
    for (u64 i = 0; member; member = member->next, i++) {
        members[i] = evalType(visitor, member);
        if (members[i] == ERROR_TYPE(ctx))
            node->type = ERROR_TYPE(ctx);
    }

    if (!typeIs(node->type, Error)) {
        if (!defineDeclaration(ctx,
                               node->typeDecl.name,
                               getDeclarationAlias(ctx, node),
                               node,
                               hasFlag(node, Public))) //
        {
            node->type = ERROR_TYPE(ctx);
        }
        else
            node->type = makeUnionType(ctx->typeTable, members, count);
    }
    
    free((void *)members);
}

void checkBuiltinType(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    switch (node->tag) {
    case astVoidType:
        node->type = makeVoidType(ctx->typeTable);
        break;
    case astAutoType:
        node->type = makeAutoType(ctx->typeTable);
        break;
    case astStringType:
        node->type = makeStringType(ctx->typeTable);
        break;
    default:
        logError(ctx->L, &node->loc, "unsupported native type", NULL);
        node->type = ERROR_TYPE(ctx);
        break;
    }
}

void checkOptionalType(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *type = evalType(visitor, node->optionalType.type);
    node->type = makeOptionalType(ctx->typeTable, type, flgNone);
}

void checkPrimitiveType(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    node->type = getPrimitiveType(ctx->typeTable, node->primitiveType.id);
}

void checkPointerType(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *pointed = evalType(visitor, node->pointerType.pointed);
    if (typeIs(pointed, Error)) {
        node->type = pointed;
        return;
    }

    node->type =
        makePointerType(ctx->typeTable, pointed, node->flags & flgConst);
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
        return comptimeCompareManyTypes(lhs->tupleType.args,
                                        rhs->tupleType.args);
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
