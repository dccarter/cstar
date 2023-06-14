//
// Created by Carter on 2023-04-27.
//

#include "lang/eval.h"
#include "lang/semantics.h"

#include "lang/ttable.h"

typedef AstNode *(*AstNodeMemberGetter)(SemanticsContext *,
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

static inline AstNode *comptimeWrapped(SemanticsContext *ctx,
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

static AstNode *getName(SemanticsContext *ctx,
                        const FileLoc *loc,
                        AstNode *node)
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

static AstNode *getElementType(SemanticsContext *ctx,
                               const FileLoc *loc,
                               AstNode *node)
{
    const Type *type =
        actualType(node->type ?: evalType(ctx->eval.semanticsVisitor, node));
    if (!isArrayType(type))
        return NULL;

    return makeTypeReferenceNode(ctx, type->array.elementType);
}

static AstNode *getPointedType(SemanticsContext *ctx,
                               const FileLoc *loc,
                               AstNode *node)
{
    const Type *type =
        actualType(node->type ?: evalType(ctx->eval.semanticsVisitor, node));
    if (!isPointerType(type))
        return NULL;

    return makeTypeReferenceNode(ctx, type->pointer.pointed);
}

static AstNode *isString(SemanticsContext *ctx,
                         const FileLoc *loc,
                         AstNode *node)
{
    node->type ?: evalType(ctx->eval.semanticsVisitor, node);
    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit,
                   .boolLiteral.value = typeIs(node->type, String)});
}

static AstNode *isChar(SemanticsContext *ctx, const FileLoc *loc, AstNode *node)
{
    node->type ?: evalType(ctx->eval.semanticsVisitor, node);
    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit,
                   .boolLiteral.value = isCharacterType(node->type)});
}

static AstNode *isArray(SemanticsContext *ctx,
                        const FileLoc *loc,
                        AstNode *node)
{
    node->type ?: evalType(ctx->eval.semanticsVisitor, node);
    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit,
                   .boolLiteral.value = isArrayType(node->type)});
}

static AstNode *isSlice(SemanticsContext *ctx,
                        const FileLoc *loc,
                        AstNode *node)
{
    node->type ?: evalType(ctx->eval.semanticsVisitor, node);
    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit,
                   .boolLiteral.value = isSliceType(node->type)});
}

static AstNode *isBoolean(SemanticsContext *ctx,
                          const FileLoc *loc,
                          AstNode *node)
{
    node->type ?: evalType(ctx->eval.semanticsVisitor, node);
    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit,
                   .boolLiteral.value = isBooleanType(node->type)});
}

static AstNode *isNumeric(SemanticsContext *ctx,
                          const FileLoc *loc,
                          AstNode *node)
{
    node->type ?: evalType(ctx->eval.semanticsVisitor, node);
    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit,
                   .boolLiteral.value = isNumericType(node->type)});
}

static AstNode *isInteger(SemanticsContext *ctx,
                          const FileLoc *loc,
                          AstNode *node)
{
    node->type ?: evalType(ctx->eval.semanticsVisitor, node);
    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit,
                   .boolLiteral.value = isIntegerType(node->type)});
}

static AstNode *isSigned(SemanticsContext *ctx,
                         const FileLoc *loc,
                         AstNode *node)
{
    node->type ?: evalType(ctx->eval.semanticsVisitor, node);
    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit,
                   .boolLiteral.value = isSignedType(node->type)});
}

static AstNode *isUnsigned(SemanticsContext *ctx,
                           const FileLoc *loc,
                           AstNode *node)
{
    node->type ?: evalType(ctx->eval.semanticsVisitor, node);
    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit,
                   .boolLiteral.value = isUnsignedType(node->type)});
}

static AstNode *isFloat(SemanticsContext *ctx,
                        const FileLoc *loc,
                        AstNode *node)
{
    node->type ?: evalType(ctx->eval.semanticsVisitor, node);
    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit,
                   .boolLiteral.value = isFloatType(node->type)});
}

static AstNode *isOptional(SemanticsContext *ctx,
                           const FileLoc *loc,
                           AstNode *node)
{
    node->type ?: evalType(ctx->eval.semanticsVisitor, node);
    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit, .boolLiteral.value = false});
}

static AstNode *isPointer(SemanticsContext *ctx,
                          const FileLoc *loc,
                          AstNode *node)
{
    node->type ?: evalType(ctx->eval.semanticsVisitor, node);
    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit,
                   .boolLiteral.value = typeIs(node->type, Pointer)});
}

static AstNode *isStruct(SemanticsContext *ctx,
                         const FileLoc *loc,
                         AstNode *node)
{
    node->type ?: evalType(ctx->eval.semanticsVisitor, node);
    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit,
                   .boolLiteral.value = typeIs(node->type, Struct)});
}

static AstNode *isEnum(SemanticsContext *ctx, const FileLoc *loc, AstNode *node)
{
    node->type ?: evalType(ctx->eval.semanticsVisitor, node);
    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit,
                   .boolLiteral.value = typeIs(node->type, Enum)});
}

static AstNode *isField(SemanticsContext *ctx,
                        const FileLoc *loc,
                        AstNode *node)
{
    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit,
                   .boolLiteral.value = nodeIs(node, StructField)});
}

static void initDefaultMembers(SemanticsContext *ctx)
{
    defaultMembers = newHashTable(sizeof(AstNodeGetMember));
#define ADD_MEMBER(name, G)                                                    \
    insertAstNodeGetter(&defaultMembers, makeString(ctx->strPool, name), G)

    ADD_MEMBER("name", getName);
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

static AstNode *getStructMembers(SemanticsContext *ctx,
                                 const FileLoc *loc,
                                 AstNode *node)
{
    return comptimeWrapped(
        ctx, loc, node->structDecl.members, flgComptimeIterable);
}

static AstNode *getStructBase(SemanticsContext *ctx,
                              const FileLoc *loc,
                              AstNode *node)
{
    if (node->structDecl.base) {
        return findSymbolOnlyByNode(&ctx->eval.env, node->structDecl.base);
    }

    logError(ctx->L,
             loc,
             "struct '{s}' does not extend any base type",
             (FormatArg[]){{.s = node->structDecl.name}});
    logNote(ctx->L, &node->loc, "struct declared here", NULL);

    return NULL;
}

static void initStructDeclMembers(SemanticsContext *ctx)
{
    structDeclMembers = newHashTable(sizeof(AstNodeGetMember));
#define ADD_MEMBER(name, G)                                                    \
    insertAstNodeGetter(&structDeclMembers, makeString(ctx->strPool, name), G)

    ADD_MEMBER("members", getStructMembers);
    ADD_MEMBER("base", getStructBase);

#undef ADD_MEMBER
}

static AstNode *getStructFieldType(SemanticsContext *ctx,
                                   const FileLoc *loc,
                                   AstNode *node)
{
    return makeTypeReferenceNode(ctx, node->type);
}

static void initStructFieldMembers(SemanticsContext *ctx)
{
    fieldDeclMembers = newHashTable(sizeof(AstNodeGetMember));
#define ADD_MEMBER(name, G)                                                    \
    insertAstNodeGetter(&fieldDeclMembers, makeString(ctx->strPool, name), G)

    ADD_MEMBER("Tinfo", getStructFieldType);

#undef ADD_MEMBER
}

void initComptime(SemanticsContext *ctx)
{
    static bool initialized = false;
    if (!initialized) {
        initDefaultMembers(ctx);
        initStructDeclMembers(ctx);
        initStructFieldMembers(ctx);
        initialized = true;
    }
}

void checkComptime(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Env *savedUp = ctx->eval.env.up;

    node->flags &= ~flgComptime;
    environmentAttachUp(&ctx->eval.env, ctx->env);
    bool status = evaluate(ctx->eval.visitor, node);
    environmentAttachUp(&ctx->eval.env, savedUp);

    if (!status) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    // Visit the evaluated node
    if (!hasFlag(node, Visited)) {
        astVisit(visitor, node);
    }
}

AstNode *evalAstNodeMemberAccess(SemanticsContext *ctx,
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
