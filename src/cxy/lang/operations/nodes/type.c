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

static u64 addUnionDecl(const Type **members, const Type *member, u64 count)
{
    if (typeIs(member, Union)) {
        for (u64 i = 0; i < member->tUnion.count; i++) {
            count = addUnionDecl(members, member->tUnion.members[i], count);
        }
        return count;
    }

    for (u64 i = 0; i < count; i++) {
        if (members[i] == member)
            return count;
    }
    members[count++] = member;
    return count;
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

void generateTypeDeclDefinition(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    if (type->alias.aliased != NULL) {
        format(state, "typedef struct _", NULL);
        writeTypename(context, type->alias.aliased);
        format(state, " ", NULL);
        writeTypename(context, type);
        format(state, ";", NULL);
    }
    else if (type->namespace != NULL) {
        format(state, "#ifndef ", NULL);
        writeTypename(context, type);
        format(state, "\n#define ", NULL);
        writeTypename(context, type);
        format(state, " {s} *\n#endif", (FormatArg[]){{.s = type->name}});
    }
}

void generateTypeDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (!hasFlags(node, flgNative | flgForwardDecl))
        generateTypeUsage(ctx, node->type);
}

void generateTypeinfo(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    csAssert0((hasFlag(node, Typeinfo)));
    writeTypename(ctx, node->type);
}

void checkTypeDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    if (!hasFlag(node, ForwardDecl) && node->typeDecl.aliased) {
        const Type *type =
            makeThisType(ctx->types, node->typeDecl.name, flgNone);
        node->type = type;
        ((Type *)type)->this.trackReferences = true;

        const Type *aliased = checkType(visitor, node->typeDecl.aliased);
        if (typeIs(aliased, Error)) {
            resolveThisReferences(ctx->types, type, NULL);
            node->type = aliased;
            return;
        }

        ((Type *)type)->this.that = node->type = makeAliasType(
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

    const Type **members_ = mallocOrDie(sizeof(Type *) * count);
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

bool comptimeCompareTypes(const AstNode *lhs, const AstNode *rhs)
{
    if (lhs->tag != rhs->tag) {
        if (nodeIs(lhs, TypeRef) || nodeIs(rhs, TypeRef))
            return lhs->type == rhs->type;
        return lhs->tag == astAutoType;
    }

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
