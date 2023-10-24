//
// Created by Carter on 2023-04-27.
//

#include "eval.h"

#include "lang/builtins.h"
#include "lang/flag.h"
#include "lang/strings.h"
#include "lang/ttable.h"

typedef AstNode *(*AstNodeMemberGetter)(EvalContext *,
                                        const FileLoc *,
                                        AstNode *);

typedef struct {
    cstring name;
    AstNodeMemberGetter get;
} AstNodeGetMember;

static HashTable structDeclMembers;
static HashTable fieldDeclMembers;
static HashTable defaultMembers;

static bool compareAstNodeGetMember(const void *lhs, const void *rhs)
{
    return ((const AstNodeGetMember *)lhs)->name ==
           ((const AstNodeGetMember *)rhs)->name;
}

static AstNodeMemberGetter findAstNodeGetter(HashTable *table, cstring name)
{
    AstNodeGetMember get = {.name = name};
    HashCode hash = hashStr(hashInit(), name);
    AstNodeGetMember *found = findInHashTable(
        table, &get, hash, sizeof(get), compareAstNodeGetMember);
    if (found)
        return found->get;
    return NULL;
}

static void insertAstNodeGetter(HashTable *table,
                                cstring name,
                                AstNodeMemberGetter getter)
{
    AstNodeGetMember get = {.name = name, .get = getter};
    HashCode hash = hashStr(hashInit(), name);
    bool found = insertInHashTable(
        table, &get, hash, sizeof(get), compareAstNodeGetMember);

    csAssert0(found);
}

static inline AstNode *comptimeWrapped(EvalContext *ctx,
                                       const FileLoc *loc,
                                       AstNode *node,
                                       u64 flags)
{
    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astComptimeOnly, .next = node, .flags = flags});
}

static inline const Type *actualType(const Type *type)
{
    if (type == NULL)
        return type;

    switch (type->tag) {
    case typWrapped:
        return unwrapType(type, NULL);
    case typInfo:
        return type->info.target;
    default:
        return type;
    }
}

static AstNode *getName(EvalContext *ctx, const FileLoc *loc, AstNode *node)
{
    if (node == NULL)
        return NULL;

    cstring name = NULL;
    switch (node->tag) {
    case astField:
        name = node->structField.name;
        break;
    case astStructDecl:
    case astClassDecl:
        name = node->structDecl.name;
        break;
    case astFuncDecl:
        name = node->funcDecl.name;
        break;
    case astFuncParam:
        name = node->funcParam.name;
        break;
    case astPrimitiveType:
        name = getPrimitiveTypeName(node->primitiveType.id);
        break;
    case astGenericParam:
        return getName(ctx, loc, getTypeDecl(stripAll(node->type)));
    default:
        return NULL;
    }

    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astStringLit, .stringLiteral.value = name});
}

static AstNode *getMembers(EvalContext *ctx,
                           attr(unused) const FileLoc *loc,
                           AstNode *node)
{
    if (node == NULL)
        return NULL;

    const Type *type = stripAll(node->type);
    switch (node->tag) {
    case astTupleType:
        return comptimeWrapped(
            ctx, &node->loc, node->tupleExpr.elements, flgComptimeIterable);
    case astStructDecl:
    case astClassDecl:
        return comptimeWrapped(
            ctx, &node->loc, node->structDecl.members, flgComptimeIterable);
    case astGenericParam:
        return getMembers(ctx, loc, getTypeDecl(type));
    default:
        break;
    }
    return NULL;
}

static AstNode *getMembersCount(EvalContext *ctx,
                                attr(unused) const FileLoc *loc,
                                AstNode *node)
{
    if (node == NULL)
        return NULL;

    const Type *type = stripAll(node->type);
    switch (type->tag) {
    case typTuple:
        return makeAstNode(
            ctx->pool,
            loc,
            &(AstNode){.tag = astIntegerLit,
                       .intLiteral.value = (i64)type->tuple.count});
    default:
        break;
    }
    return NULL;
}

static AstNode *getTypeInfo(EvalContext *ctx,
                            attr(unused) const FileLoc *loc,
                            AstNode *node)
{
    switch (node->tag) {
    case astFuncParam:
        if (hasFlag(node, Variadic))
            return comptimeWrapped(ctx,
                                   &node->loc,
                                   node->funcParam.type->tupleType.elements,
                                   flgComptimeIterable);

        return node->funcParam.type;
    default:
        break;
    }
    return NULL;
}

static AstNode *getElementType(EvalContext *ctx,
                               const FileLoc *loc,
                               AstNode *node)
{
    const Type *type = actualType(node->type ?: evalType(ctx, node));
    if (!isArrayType(type))
        return NULL;

    return makeTypeReferenceNode(ctx->pool, type->array.elementType, loc);
}

static AstNode *getStripedType(EvalContext *ctx,
                               const FileLoc *loc,
                               AstNode *node)
{
    const Type *type = stripAll(node->type ?: evalType(ctx, node));
    return makeTypeReferenceNode(ctx->pool, type, loc);
}

static AstNode *makePointedTypeAstNode(EvalContext *ctx,
                                       const FileLoc *loc,
                                       AstNode *node)
{
    const Type *type = actualType(node->type ?: evalType(ctx, node));
    if (!isPointerType(type))
        return NULL;

    return makeTypeReferenceNode(ctx->pool, type->pointer.pointed, loc);
}

static AstNode *isString(EvalContext *ctx, const FileLoc *loc, AstNode *node)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveType(unwrapType(type, NULL));

    return makeAstNode(ctx->pool,
                       loc,
                       &(AstNode){.tag = astBoolLit,
                                  .boolLiteral.value = typeIs(type, String) ||
                                                       isBuiltinString(type)});
}

static AstNode *isChar(EvalContext *ctx, const FileLoc *loc, AstNode *node)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveType(type);

    return makeAstNode(ctx->pool,
                       loc,
                       &(AstNode){.tag = astBoolLit,
                                  .boolLiteral.value = isCharacterType(type)});
}

static AstNode *isArray(EvalContext *ctx, const FileLoc *loc, AstNode *node)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveType(type);

    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit, .boolLiteral.value = isArrayType(type)});
}

static AstNode *isSlice(EvalContext *ctx, const FileLoc *loc, AstNode *node)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveType(type);

    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit, .boolLiteral.value = isSliceType(type)});
}

static AstNode *isBoolean(EvalContext *ctx, const FileLoc *loc, AstNode *node)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveType(type);

    return makeAstNode(ctx->pool,
                       loc,
                       &(AstNode){.tag = astBoolLit,
                                  .boolLiteral.value = isBooleanType(type)});
}

static AstNode *isNumeric(EvalContext *ctx, const FileLoc *loc, AstNode *node)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveType(type);

    return makeAstNode(ctx->pool,
                       loc,
                       &(AstNode){.tag = astBoolLit,
                                  .boolLiteral.value = isNumericType(type)});
}

static AstNode *isInteger(EvalContext *ctx, const FileLoc *loc, AstNode *node)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveType(type);

    return makeAstNode(ctx->pool,
                       loc,
                       &(AstNode){.tag = astBoolLit,
                                  .boolLiteral.value = isIntegerType(type)});
}

static AstNode *isSigned(EvalContext *ctx, const FileLoc *loc, AstNode *node)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveType(type);

    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit, .boolLiteral.value = isSignedType(type)});
}

static AstNode *isUnsigned(EvalContext *ctx, const FileLoc *loc, AstNode *node)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveType(type);

    return makeAstNode(ctx->pool,
                       loc,
                       &(AstNode){.tag = astBoolLit,
                                  .boolLiteral.value = isUnsignedType(type)});
}

static AstNode *isFloat(EvalContext *ctx, const FileLoc *loc, AstNode *node)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveType(type);

    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit, .boolLiteral.value = isFloatType(type)});
}

static AstNode *isOptional(EvalContext *ctx, const FileLoc *loc, AstNode *node)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveType(type);

    bool isOptional = typeIs(type, Struct) && hasFlag(type, Optional);
    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit, .boolLiteral.value = isOptional});
}

static AstNode *isPointer(EvalContext *ctx, const FileLoc *loc, AstNode *node)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveType(type);

    return makeAstNode(ctx->pool,
                       loc,
                       &(AstNode){.tag = astBoolLit,
                                  .boolLiteral.value = typeIs(type, Pointer)});
}

static AstNode *isStruct(EvalContext *ctx, const FileLoc *loc, AstNode *node)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = unwrapType(resolveType(type), NULL);

    return makeAstNode(ctx->pool,
                       loc,
                       &(AstNode){.tag = astBoolLit,
                                  .boolLiteral.value = typeIs(type, Struct)});
}

static AstNode *isClass(EvalContext *ctx, const FileLoc *loc, AstNode *node)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = unwrapType(resolveType(type), NULL);

    return makeAstNode(ctx->pool,
                       loc,
                       &(AstNode){.tag = astBoolLit,
                                  .boolLiteral.value = typeIs(type, Class)});
}

static AstNode *evalIsTupleType(EvalContext *ctx,
                                const FileLoc *loc,
                                AstNode *node)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = unwrapType(resolveType(type), NULL);

    return makeAstNode(ctx->pool,
                       loc,
                       &(AstNode){.tag = astBoolLit,
                                  .boolLiteral.value = typeIs(type, Tuple)});
}

static AstNode *isEnum(EvalContext *ctx, const FileLoc *loc, AstNode *node)
{
    node->type ?: evalType(ctx, node);
    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit,
                   .boolLiteral.value = typeIs(node->type, Enum)});
}

static AstNode *isDestructible(EvalContext *ctx,
                               const FileLoc *loc,
                               AstNode *node)
{

    const Type *type = node->type ?: evalType(ctx, node);
    type = unwrapType(type, NULL);
    AstNode *decl = getTypeDecl(type);

    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit,
                   .boolLiteral.value = isClassOrStructType(type) &&
                                        hasFlag(decl, ImplementsDeinit)});
}

static AstNode *isCover(EvalContext *ctx, const FileLoc *loc, AstNode *node)
{

    const Type *type = node->type ?: evalType(ctx, node);
    type = unwrapType(type, NULL);
    AstNode *decl = getTypeDecl(type);

    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit,
                   .boolLiteral.value =
                       isClassOrStructType(type) && hasFlag(decl, Native)});
}

static AstNode *isField(EvalContext *ctx, const FileLoc *loc, AstNode *node)
{
    return makeAstNode(ctx->pool,
                       loc,
                       &(AstNode){.tag = astBoolLit,
                                  .boolLiteral.value = nodeIs(node, Field)});
}

static void initDefaultMembers(EvalContext *ctx)
{
    defaultMembers = newHashTable(sizeof(AstNodeGetMember));
#define ADD_MEMBER(name, G)                                                    \
    insertAstNodeGetter(&defaultMembers, makeString(ctx->strings, name), G)

    ADD_MEMBER("name", getName);
    ADD_MEMBER("members", getMembers);
    ADD_MEMBER("Tinfo", getTypeInfo);
    ADD_MEMBER("elementType", getElementType);
    ADD_MEMBER("pointedType", makePointedTypeAstNode);
    ADD_MEMBER("strippedType", getStripedType);
    ADD_MEMBER("membersCount", getMembersCount);
    ADD_MEMBER("isInteger", isInteger);
    ADD_MEMBER("isSigned", isSigned);
    ADD_MEMBER("isUnsigned", isUnsigned);
    ADD_MEMBER("isFloat", isFloat);
    ADD_MEMBER("isOptional", isOptional);
    ADD_MEMBER("isNumber", isNumeric);
    ADD_MEMBER("isPointer", isPointer);
    ADD_MEMBER("isStruct", isStruct);
    ADD_MEMBER("isClass", isClass);
    ADD_MEMBER("isTuple", evalIsTupleType);
    ADD_MEMBER("isField", isField);
    ADD_MEMBER("isString", isString);
    ADD_MEMBER("isBoolean", isBoolean);
    ADD_MEMBER("isChar", isChar);
    ADD_MEMBER("isArray", isArray);
    ADD_MEMBER("isSlice", isSlice);
    ADD_MEMBER("isEnum", isEnum);
    ADD_MEMBER("isDestructible", isDestructible);
    ADD_MEMBER("isCover", isCover);

#undef ADD_MEMBER
}

static AstNode *getStructOrClassMembers(EvalContext *ctx,
                                        const FileLoc *loc,
                                        AstNode *node)
{
    return comptimeWrapped(
        ctx, loc, node->structDecl.members, flgComptimeIterable);
}

static AstNode *getClassBase(EvalContext *ctx,
                             const FileLoc *loc,
                             AstNode *node)
{
    if (node->classDecl.base) {
        return getResolvedPath(node->classDecl.base);
    }

    logError(ctx->L,
             loc,
             "class '{s}' does not extend any base type",
             (FormatArg[]){{.s = node->classDecl.name}});
    logNote(ctx->L, &node->loc, "class declared here", NULL);

    return NULL;
}

static void initStructDeclMembers(EvalContext *ctx)
{
    structDeclMembers = newHashTable(sizeof(AstNodeGetMember));
#define ADD_MEMBER(name, G)                                                    \
    insertAstNodeGetter(&structDeclMembers, makeString(ctx->strings, name), G)

    ADD_MEMBER("members", getStructOrClassMembers);
    ADD_MEMBER("base", getClassBase);

#undef ADD_MEMBER
}

static AstNode *getStructFieldType(EvalContext *ctx,
                                   const FileLoc *loc,
                                   AstNode *node)
{
    return makeTypeReferenceNode(ctx->pool, node->type, loc);
}

static void initStructFieldMembers(EvalContext *ctx)
{
    fieldDeclMembers = newHashTable(sizeof(AstNodeGetMember));
#define ADD_MEMBER(name, G)                                                    \
    insertAstNodeGetter(&fieldDeclMembers, makeString(ctx->strings, name), G)

    ADD_MEMBER("Tinfo", getStructFieldType);

#undef ADD_MEMBER
}

void initComptime(EvalContext *ctx)
{
    static bool initialized = false;
    if (!initialized) {
        initDefaultMembers(ctx);
        initStructDeclMembers(ctx);
        initStructFieldMembers(ctx);
        initialized = true;
    }
}

AstNode *evalAstNodeMemberAccess(EvalContext *ctx,
                                 const FileLoc *loc,
                                 AstNode *node,
                                 cstring name)
{
    HashTable *table = NULL;
    switch (node->tag) {
    case astStructDecl:
        table = &structDeclMembers;
        break;
    case astField:
        table = &fieldDeclMembers;
        break;
    default:
        break;
    }

    AstNodeMemberGetter getter =
        findAstNodeGetter(table ?: &defaultMembers, name)
            ?: (table != NULL ? findAstNodeGetter(&defaultMembers, name)
                              : NULL);
    if (getter == NULL)
        return NULL;

    return getter(ctx, loc, node);
}
