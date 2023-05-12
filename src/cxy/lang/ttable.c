//
// Created by Carter on 2023-03-22.
//

#include "ttable.h"
#include "ast.h"
#include "scope.h"

#include <core/alloc.h>
#include <core/htable.h>
#include <core/mempool.h>
#include <core/strpool.h>

#include <memory.h>

static HashCode hashTypes(HashCode hash, const Type **types, u64 count)
{
    for (u64 i = 0; i < count; i++)
        hash = hashUint64(hash, types[i]->tag);
    return hash;
}

static HashCode hashType(HashCode hash, const Type *type)
{
    hash = hashUint32(hash, type->tag);
    hash = hashUint64(hash, type->flags);
    //    if (type->namespace)
    //        hash = hashStr(hash, type->namespace);

    switch (type->tag) {
    case typAuto:
    case typNull:
    case typVoid:
    case typError:
    case typThis:
        break;
    case typPrimitive:
        hash = hashUint32(hash, type->primitive.id);
        break;
    case typString:
        break;
    case typPointer:
        hash = hashType(hash, type->pointer.pointed);
        break;
    case typArray:
        hash = hashType(hash, type->array.elementType);
        hash = hashUint64(hash, type->array.len);
        break;
    case typMap:
        hash = hashType(hash, type->map.key);
        hash = hashType(hash, type->map.value);
        break;
    case typAlias:
        hash = hashType(hash, type->alias.aliased);
        break;
    case typOpaque:
        hash = hashStr(hash, type->name);
        break;
    case typOptional:
    case typInfo:
    case typWrapped:
        hash = hashType(hash, type->optional.target);
        break;
    case typUnion:
    case typTuple:
        hash = hashTypes(hash, type->tuple.members, type->tuple.count);
        break;
    case typFunc:
        hash = hashTypes(hash, type->func.params, type->func.paramsCount);
        hash = hashType(hash, type->func.retType);
        break;
    case typEnum:
    case typStruct:
    case typModule:
        if (type->name)
            hash = hashStr(hash, type->name);
        break;
    case typGeneric:
        hash = hashUint64(hash, type->generic.paramsCount);
        if (type->name)
            hash = hashStr(hash, type->name);
        break;
    case typApplied:
        hash = hashType(hash, type->applied.from);
        hash = hashTypes(hash, type->applied.args, type->applied.argsCount);
        break;
    default:
        csAssert0("invalid type");
    }

    return hash;
}

static bool compareTypes(const Type *left, const Type *right);

static bool compareManyTypes(const Type **left, const Type **right, u64 count)
{
    for (u64 i = 0; i < count; i++) {
        if (!compareTypes(left[i], right[i]))
            return false;
    }
    return true;
}

static bool compareTypes(const Type *left, const Type *right)
{
    if (left->tag != right->tag)
        return false;
    switch (left->tag) {
    case typAuto:
    case typError:
    case typVoid:
    case typNull:
        break;
    case typPrimitive:
        return left->primitive.id == right->primitive.id;
    case typPointer:
        return ((left->flags & flgConst) == (right->flags & flgConst)) &&
               compareTypes(left->pointer.pointed, right->pointer.pointed);
    case typArray:
        return (left->array.len == right->array.len) &&
               compareTypes(left->array.elementType, right->array.elementType);
    case typMap:
        return compareTypes(left->map.key, right->map.key) &&
               compareTypes(left->map.value, right->map.value);
    case typAlias:
        return compareTypes(left->alias.aliased, right->alias.aliased);
    case typOptional:
        return compareTypes(left->optional.target, right->optional.target);
    case typOpaque:
        return strcmp(left->name, right->name) == 0;
    case typThis:
        return typeIs(right, This) ? (left == right) : left->this.that == right;
    case typTuple:
    case typUnion:
        return (left->tUnion.count == right->tUnion.count) &&
               compareManyTypes(left->tUnion.members,
                                right->tUnion.members,
                                left->tUnion.count);
    case typFunc:
        if (left->name && right->name && strcmp(left->name, right->name) != 0)
            return false;
        if (left->func.decl && right->func.decl &&
            left->func.decl->parentScope && right->func.decl->parentScope &&
            left->func.decl->parentScope != right->func.decl->parentScope)
            return false;

        return ((left->flags & flgVariadic) == (right->flags & flgVariadic)) &&
               (left->func.paramsCount == right->func.paramsCount) &&
               compareTypes(left->func.retType, right->func.retType) &&
               compareManyTypes(left->func.params,
                                right->func.params,
                                right->func.paramsCount);
    case typApplied:
        return compareTypes(left->applied.from, right->applied.from) &&
               compareManyTypes(left->applied.args,
                                right->applied.args,
                                right->applied.argsCount);
    case typEnum:
    case typStruct:
    case typGeneric:
    case typInfo:
        return left == right;

    default:
        csAssert0("invalid type");
    }

    return true;
}

static const Type **copyTypes(TypeTable *table, const Type **types, u64 count)
{
    const Type **dest =
        allocFromMemPool(table->memPool, sizeof(Type *) * count);
    memcpy(dest, types, sizeof(Type *) * count);
    return dest;
}

static bool compareTypesWrapper(const void *left, const void *right)
{
    return compareTypes(*(const Type **)left, *(const Type **)right);
}

static GetOrInset getOrInsertTypeScoped(TypeTable *table, const Type *type)
{
    u32 hash = hashType(hashInit(), type);
    const Type **found = findInHashTable(&table->types, //
                                         &type,
                                         hash,
                                         sizeof(Type *),
                                         compareTypesWrapper);
    if (found)
        return (GetOrInset){true, *found};

    Type *newType = New(table->memPool, Type);
    memcpy(newType, type, sizeof(Type));

    if (!insertInHashTable(
            &table->types, &newType, hash, sizeof(Type *), compareTypesWrapper))
        csAssert0("failing to insert in type table");

    newType->index = table->typeCount++;
    return (GetOrInset){false, newType};
}

static GetOrInset getOrInsertType(TypeTable *table, const Type *type)
{
    Type tmp = *type;
    tmp.namespace = table->currentNamespace;
    return getOrInsertTypeScoped(table, &tmp);
}

typedef struct {
    const Type **types;
    u64 count;
} SortTypesContext;

static bool countTypesWrapper(void *ctx, const void *elem)
{
    SortTypesContext *context = ctx;
    const Type *type = *(const Type **)elem;
    if (type->index < context->count)
        context->types[type->index] = type;
    return true;
}

TypeTable *newTypeTable(MemPool *pool, StrPool *strPool)
{
    TypeTable *table = mallocOrDie(sizeof(TypeTable));
    table->types = newHashTable(sizeof(Type *));
    table->strPool = strPool;
    table->memPool = pool;
    table->typeCount = 0;
    for (u64 i = 0; i < prtCOUNT; i++)
        table->primitiveTypes[i] =
            getOrInsertType(table,
                            &make(Type,
                                  .tag = typPrimitive,
                                  .size = getPrimitiveTypeSize(i),
                                  .name = getPrimitiveTypeName(i),
                                  .primitive.id = i))
                .s;

    table->errorType = getOrInsertType(table, &make(Type, .tag = typError)).s;
    table->autoType = getOrInsertType(table, &make(Type, .tag = typAuto)).s;
    table->voidType = getOrInsertType(table, &make(Type, .tag = typVoid)).s;
    table->_nullType = getOrInsertType(table, &make(Type, .tag = typNull)).s;
    table->nullType = makePointerType(table, table->_nullType, flgNone);
    table->stringType = getOrInsertType(table, &make(Type, .tag = typString)).s;
    table->anySliceType =
        getOrInsertType(
            table,
            &make(Type,
                  .tag = typArray,
                  .flags = flgBuiltin,
                  .array = {.elementType = table->autoType, .len = UINT64_MAX}))
            .s;

    return table;
}

void freeTypeTable(TypeTable *table)
{
    freeHashTable(&table->types);
    free(table);
}

void removeFromTypeTable(TypeTable *table, const Type *type)
{
    u32 hash = hashType(hashInit(), type);
    const Type **found = findInHashTable(&table->types, //
                                         &type,
                                         hash,
                                         sizeof(Type *),
                                         compareTypesWrapper);
    if (found)
        removeFromHashTable(&table->types, found, sizeof(Type *));
}

const Type *resolveType(const Type *type)
{
    while (type) {
        switch (type->tag) {
        case typAlias:
            type = resolveType(type->alias.aliased);
            break;
        default:
            return type;
        }
    }
    return NULL;
}

const Type *stripPointer(const Type *type)
{
    while (true) {
        switch (resolveType(type)->tag) {
        case typPointer:
            type = stripPointer(type->pointer.pointed);
            break;
        default:
            return type;
        }
    }
}

const Type *stripAll(const Type *type)
{
    while (true) {
        switch (resolveType(type)->tag) {
        case typPointer:
            type = stripAll(type->pointer.pointed);
            break;
        case typWrapped:
            type = stripAll(type->wrapped.target);
            break;
        default:
            return type;
        }
    }
}

const Type *arrayToPointer(TypeTable *table, const Type *type)
{
    if (type->tag != typArray)
        return type;

    return makePointerType(
        table, arrayToPointer(table, type->array.elementType), type->flags);
}

const Type *makeErrorType(TypeTable *table) { return table->errorType; }

const Type *makeAutoType(TypeTable *table) { return table->autoType; }

const Type *makeVoidType(TypeTable *table) { return table->voidType; }

const Type *makeNullType(TypeTable *table) { return table->nullType; }

const Type *makeStringType(TypeTable *table) { return table->stringType; }

const Type *getPrimitiveType(TypeTable *table, PrtId id)
{
    csAssert(id != prtCOUNT, "");
    return table->primitiveTypes[id];
}

const Type *getAnySliceType(TypeTable *table) { return table->anySliceType; }

const Type *makeArrayType(TypeTable *table,
                          const Type *elementType,
                          const u64 size)
{
    Type type = make(Type,
                     .tag = typArray,
                     .array = {.elementType = elementType, .len = size});

    return getOrInsertType(table, &type).s;
}

const Type *makeTypeInfo(TypeTable *table, const Type *target)
{
    Type type = make(Type, .tag = typInfo, .info = {.target = target});

    return getOrInsertType(table, &type).s;
}

const Type *makePointerType(TypeTable *table, const Type *pointed, u64 flags)
{
    Type type = make(Type,
                     .tag = typPointer,
                     .flags = flags,
                     .pointer = {.pointed = pointed});

    return getOrInsertType(table, &type).s;
}

const Type *makeOptionalType(TypeTable *table, const Type *target, u64 flags)
{
    Type type = make(Type,
                     .tag = typOptional,
                     .flags = flags,
                     .optional = {.target = target});

    return getOrInsertType(table, &type).s;
}

const Type *makeMapType(TypeTable *table, const Type *key, const Type *value)
{
    Type type = make(Type, .tag = typMap, .map = {.key = key, .value = value});

    return getOrInsertType(table, &type).s;
}

const Type *makeAliasType(TypeTable *table, const Type *aliased, cstring name)
{
    Type type = make(
        Type, .tag = typAlias, .name = name, .alias = {.aliased = aliased});
    return getOrInsertType(table, &type).s;
}

const Type *makeOpaqueType(TypeTable *table, cstring name)
{
    Type type = make(Type, .tag = typOpaque, .name = name, .size = 0);

    return getOrInsertType(table, &type).s;
}

const Type *makeUnionType(TypeTable *table, const Type **members, u64 count)
{
    Type type = make(
        Type, .tag = typUnion, .tUnion = {.members = members, .count = count});

    GetOrInset ret = getOrInsertType(table, &type);
    if (!ret.f) {
        ((Type *)ret.s)->tUnion.members = copyTypes(table, members, count);
    }

    return ret.s;
}

const Type *makeTupleType(TypeTable *table,
                          const Type **members,
                          u64 count,
                          u64 flags)
{
    Type type = make(Type,
                     .tag = typTuple,
                     .flags = flags,
                     .tuple = {.members = members, .count = count});

    GetOrInset ret = getOrInsertType(table, &type);
    if (!ret.f) {
        ((Type *)ret.s)->tuple.members = copyTypes(table, members, count);
    }

    return ret.s;
}

const Type *makeThisType(TypeTable *table, cstring name, u64 flags)
{
    Type type = make(Type, .tag = typThis, .name = name, .flags = flags);
    return getOrInsertType(table, &type).s;
}

const Type *makeFuncType(TypeTable *table, const Type *init)
{
    GetOrInset ret = getOrInsertType(table, init);
    if (!ret.f) {
        ((Type *)ret.s)->func.params =
            copyTypes(table, init->func.params, init->func.paramsCount);
    }

    return ret.s;
}

const Type *makeEnum(TypeTable *table, const Type *init)
{
    GetOrInset ret = getOrInsertType(table, init);
    if (!ret.f) {
        Type *tEnum = (Type *)ret.s;
        tEnum->tEnum.options = allocFromMemPool(
            table->memPool, sizeof(EnumOption) * init->tEnum.count);
        memcpy(tEnum->tEnum.options,
               init->tEnum.options,
               sizeof(EnumOption) * init->tEnum.count);
        tEnum->tEnum.env = allocFromMemPool(table->memPool, sizeof(Env));
        *tEnum->tEnum.env = *init->tEnum.env;
    }

    return ret.s;
}

const Type *makeGenericType(TypeTable *table, const Type *init)
{
    GetOrInset ret = getOrInsertType(table, init);
    if (!ret.f) {
        Type *generic = (Type *)ret.s;
        generic->generic.params = allocFromMemPool(
            table->memPool, sizeof(GenericParam) * init->generic.paramsCount);
        memcpy(generic->generic.params,
               init->generic.params,
               sizeof(GenericParam) * init->generic.paramsCount);
    }

    return ret.s;
}

const Type *makeWrappedType(TypeTable *table, const Type *target, u64 flags)
{
    if ((target->flags & flags) == flags)
        return target;
    Type type = {.tag = typWrapped, .flags = flags, .wrapped.target = target};
    return getOrInsertType(table, &type).s;
}

const Type *unwrapType(const Type *type, u64 *flags)
{
    u64 tmp = flgNone;

    while (typeIs(type, Wrapped)) {
        tmp |= type->flags;
        type = type->wrapped.target;
    }

    if (flags)
        *flags = tmp | type->flags;

    return type;
}

GetOrInset makeAppliedType(TypeTable *table, const Type *init)
{
    GetOrInset ret = getOrInsertType(table, init);
    if (!ret.f) {
        Type *applied = (Type *)ret.s;
        applied->applied.args = allocFromMemPool(
            table->memPool, sizeof(Type) * init->applied.argsCount);
        memcpy(applied->applied.args,
               init->applied.args,
               sizeof(Type) * init->applied.argsCount);
    }

    return ret;
}

const Type *makeStruct(TypeTable *table, const Type *init)
{
    GetOrInset ret = getOrInsertType(table, init);
    if (!ret.f) {
        Type *tStruct = (Type *)ret.s;
        tStruct->tStruct.fields = allocFromMemPool(
            table->memPool, sizeof(StructField) * init->tStruct.fieldsCount);
        memcpy(tStruct->tStruct.fields,
               init->tStruct.fields,
               sizeof(StructField) * init->tStruct.fieldsCount);
    }

    return ret.s;
}

const Type *makeModuleType(TypeTable *table, cstring name)
{
    Type type = make(Type, .tag = typModule, .name = name, .flags = flgNone);
    GetOrInset ret = getOrInsertType(table, &type);
    return ret.s;
}

u64 getTypesCount(TypeTable *table) { return table->typeCount; }

u64 sortedByInsertionOrder(TypeTable *table, const Type **types, u64 size)
{
    SortTypesContext context = {.types = types,
                                .count = MIN(size, table->typeCount)};

    enumerateHashTable(
        &table->types, &context, countTypesWrapper, sizeof(Type *));

    return context.count;
}

void enumerateTypeTable(TypeTable *table,
                        void *ctx,
                        bool(with)(void *, const void *))
{
    enumerateHashTable(&table->types, ctx, with, sizeof(Type *));
}

const Type *promoteType(TypeTable *table, const Type *left, const Type *right)
{
    left = resolveType(left);
    right = resolveType(right);
    const Type *_left = unwrapType(left, NULL),
               *_right = unwrapType(right, NULL);

    if (_left == _right)
        return left;

    if ((typeIs(_left, String) || typeIs(_left, Pointer)) &&
        typeIs(stripPointer(_right), Null))
        return left;

    left = _left, right = _right;
    switch (left->tag) {
    case typPrimitive:
        switch (left->primitive.id) {
#define f(T, ...) case prt##T:
            INTEGER_TYPE_LIST(f)
            if (isIntegerType(right))
                return left->size >= right->size ? left : right;
            if (isFloatType(right))
                return right;
            if (right->primitive.id == prtChar)
                return left->size >= 4 ? left : getPrimitiveType(table, prtU32);
            return NULL;

            FLOAT_TYPE_LIST(f)
            if (isFloatType(right))
                return left->size >= right->size ? left : right;
            if (isIntegerType(right) || right->tag == prtChar)
                return left;

#undef f
        case prtChar:
            if (isIntegerType(right))
                return right->size >= 4 ? right
                                        : getPrimitiveType(table, prtU32);
            return NULL;
        default:
            return NULL;
        }
    default:
        return NULL;
    }
}
