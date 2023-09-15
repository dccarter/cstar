//
// Created by Carter on 2023-04-27.
//

#include "eval.h"

#include "lang/flag.h"
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
    cstring name = NULL;
    switch (node->tag) {
    case astStructField:
        name = node->structField.name;
        break;
    case astStructDecl:
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
    switch (node->tag) {
    case astTupleType:
        return comptimeWrapped(
            ctx, &node->loc, node->tupleExpr.elements, flgComptimeIterable);
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

static AstNode *getPointedType(EvalContext *ctx,
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
    type = resolveType(type);

    return makeAstNode(ctx->pool,
                       loc,
                       &(AstNode){.tag = astBoolLit,
                                  .boolLiteral.value = typeIs(type, String)});
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
    type = resolveType(type);

    return makeAstNode(ctx->pool,
                       loc,
                       &(AstNode){.tag = astBoolLit,
                                  .boolLiteral.value = typeIs(type, Struct)});
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

static AstNode *isField(EvalContext *ctx, const FileLoc *loc, AstNode *node)
{
    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit,
                   .boolLiteral.value = nodeIs(node, StructField)});
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
    ADD_MEMBER("pointedType", getPointedType);
    ADD_MEMBER("isInteger", isInteger);
    ADD_MEMBER("isSigned", isSigned);
    ADD_MEMBER("isUnsigned", isUnsigned);
    ADD_MEMBER("isFloat", isFloat);
    ADD_MEMBER("isOptional", isOptional);
    ADD_MEMBER("isNumber", isNumeric);
    ADD_MEMBER("isPointer", isPointer);
    ADD_MEMBER("isStruct", isStruct);
    ADD_MEMBER("isField", isField);
    ADD_MEMBER("isString", isString);
    ADD_MEMBER("isBoolean", isBoolean);
    ADD_MEMBER("isChar", isChar);
    ADD_MEMBER("isArray", isArray);
    ADD_MEMBER("isSlice", isSlice);
    ADD_MEMBER("isEnum", isEnum);

#undef ADD_MEMBER
}

static AstNode *getStructMembers(EvalContext *ctx,
                                 const FileLoc *loc,
                                 AstNode *node)
{
    return comptimeWrapped(
        ctx, loc, node->structDecl.members, flgComptimeIterable);
}

static AstNode *getStructBase(EvalContext *ctx,
                              const FileLoc *loc,
                              AstNode *node)
{
    if (node->structDecl.base) {
        return getResolvedPath(node->structDecl.base);
    }

    logError(ctx->L,
             loc,
             "struct '{s}' does not extend any base type",
             (FormatArg[]){{.s = node->structDecl.name}});
    logNote(ctx->L, &node->loc, "struct declared here", NULL);

    return NULL;
}

static void initStructDeclMembers(EvalContext *ctx)
{
    structDeclMembers = newHashTable(sizeof(AstNodeGetMember));
#define ADD_MEMBER(name, G)                                                    \
    insertAstNodeGetter(&structDeclMembers, makeString(ctx->strings, name), G)

    ADD_MEMBER("members", getStructMembers);
    ADD_MEMBER("base", getStructBase);

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
    case astStructField:
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