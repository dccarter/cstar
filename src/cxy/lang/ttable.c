//
// Created by Carter on 2023-03-22.
//

#include "ttable.h"
#include "ast.h"
#include "flag.h"
#include "scope.h"
#include "strings.h"

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
    hash = hashUint64(hash, (type->flags & flgTypeApplicable));

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
    case typContainer:
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
        if (type->name)
            hash = hashStr(hash, type->name);
        hash = hashTypes(hash, type->func.params, type->func.paramsCount);
        hash = hashType(hash, type->func.retType);
        break;
    case typEnum:
    case typStruct:
    case typModule:
        if (type->name)
            hash = hashStr(hash, type->name);
        if (type->namespace)
            hash = hashStr(hash, type->namespace);
        break;
    case typGeneric:
        hash = hashUint64(hash, type->generic.paramsCount);
        if (type->name)
            hash = hashStr(hash, type->name);
        break;
    case typApplied:
        hash = hashType(hash, type->applied.from);
        hash =
            hashTypes(hash, type->applied.args, type->applied.totalArgsCount);
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

static bool compareTypes(const Type *lhs, const Type *rhs)
{
    u64 lhsFlags = flgNone, rhsFlags = flgNone;
    const Type *left = unwrapType(lhs, &lhsFlags),
               *right = unwrapType(rhs, &rhsFlags);

    if (left == right)
        return lhsFlags == rhsFlags;

    if (left->tag != right->tag || (lhsFlags != rhsFlags))
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
        return compareTypes(left->pointer.pointed, right->pointer.pointed);
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
        if (left->name != right->name)
            return false;

        if (left->name != NULL) {
            if (left->func.decl && right->func.decl &&
                left->func.decl->parentScope && right->func.decl->parentScope &&
                left->func.decl->parentScope != right->func.decl->parentScope)
                return false;
        }

        return (left->func.paramsCount == right->func.paramsCount) &&
               compareTypes(left->func.retType, right->func.retType) &&
               compareManyTypes(left->func.params,
                                right->func.params,
                                right->func.paramsCount);
    case typApplied:
        return compareTypes(left->applied.from, right->applied.from) &&
               right->applied.totalArgsCount == left->applied.totalArgsCount &&
               compareManyTypes(left->applied.args,
                                right->applied.args,
                                right->applied.totalArgsCount);
    case typString:
        return typeIs(right, String) || typeIs(right, Null);

    case typModule:
        return typeIs(right, Module) && left->module.path == right->module.path;

    case typEnum:
    case typStruct:
    case typGeneric:
    case typInfo:
    case typInterface:
    case typClass:
        return (left->name == right->name) &&
               (left->namespace == right->namespace);

    default:
        unreachable("invalid type");
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

static Type *replaceTypeScoped(TypeTable *table,
                               const Type *type,
                               const Type *with)
{
    u32 hash = hashType(hashInit(), type);
    Type **found = findInHashTable(&table->types, //
                                   &type,
                                   hash,
                                   sizeof(Type *),
                                   compareTypesWrapper);
    Type *newType = NULL;
    if (found) {
        newType = *found;
        removeFromTypeTable(table, *found);
    }
    else {
        newType = New(table->memPool, Type);
        newType->index = table->typeCount++;
    }

    memcpy(newType, with, sizeof(Type));

    if (!insertInHashTable(&table->types,
                           &newType,
                           hashType(hashInit(), newType),
                           sizeof(Type *),
                           compareTypesWrapper))
        csAssert0("failing to insert in type table");

    return newType;
}

static GetOrInset getOrInsertType(TypeTable *table, const Type *type)
{
    Type tmp = *type;
    tmp.namespace = table->currentNamespace;
    return getOrInsertTypeScoped(table, &tmp);
}

static int sortCompareStructMember(const void *lhs, const void *rhs)
{
    const NamedTypeMember *left = *((const NamedTypeMember **)lhs),
                          *right = *((const NamedTypeMember **)rhs);
    return left->name == right->name ? 0 : strcmp(left->name, right->name);
}

static int sortCompareEnumOption(const void *lhs, const void *rhs)
{
    const EnumOption *left = *((const EnumOption **)lhs),
                     *right = *((const EnumOption **)rhs);
    return left->name == right->name ? 0 : strcmp(left->name, right->name);
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

const Type *removeFromTypeTable(TypeTable *table, const Type *type)
{
    u32 hash = hashType(hashInit(), type);
    const Type **found = findInHashTable(&table->types, //
                                         &type,
                                         hash,
                                         sizeof(Type *),
                                         compareTypesWrapper);
    if (found) {
        removeFromHashTable(&table->types, found, sizeof(Type *));
        return *found;
    }
    return NULL;
}

const Type *resolveType(const Type *type)
{
    while (type) {
        switch (type->tag) {
        case typAlias:
            type = resolveType(type->alias.aliased);
            break;
        case typInfo:
            type = resolveType(type->info.target);
            break;
        case typOpaque:
            if (hasFlag(type, ForwardDecl)) {
                return resolveType(type->opaque.decl->type) ?: type;
            }
            return type;
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
    while (type) {
        switch (resolveType(type)->tag) {
        case typPointer:
            type = stripAll(type->pointer.pointed);
            break;
        case typWrapped:
            type = stripAll(type->wrapped.target);
            break;
        case typThis:
            if (type->this.that == NULL)
                return type;
            type = stripAll(type->this.that);
            break;
        default:
            return type;
        }
    }
    return NULL;
}

const Type *stripOnce(const Type *type, u64 *flags)
{
    type = resolveType(type);
    if (typeIs(type, Wrapped)) {
        if (flags)
            *flags |= type->flags;
        type = type->wrapped.target;
    }

    if (typeIs(type, Pointer)) {
        if (flags)
            *flags |= type->flags;
        type = type->pointer.pointed;
    }

    if (flags)
        *flags |= type->flags;

    return type;
}

u64 pointerLevels(const Type *type)
{
    u64 levels = 0;
    while (typeIs(type, Pointer)) {
        type = type->pointer.pointed;
        levels++;
    }

    return levels;
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

const Type *makeContainerType(
    TypeTable *table, cstring name, const Type *base, cstring *names, u64 count)
{
    names = allocFromMemPool(table->memPool, sizeof(cstring) * count);
    Type type = make(Type,
                     .tag = typContainer,
                     .name = name,
                     .container = {.base = base, .namesCount = count});

    GetOrInset goi = getOrInsertType(table, &type);
    if (!goi.f) {
        Type *inserted = ((Type *)goi.s);
        inserted->container.names =
            allocFromMemPool(table->memPool, sizeof(cstring) * count);
        memcpy(inserted->container.names, names, sizeof(cstring) * count);
    }

    return goi.s;
}

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
    pointed = flattenWrappedType(pointed, &flags);
    Type type = make(Type,
                     .tag = typPointer,
                     .flags = flags,
                     .pointer = {.pointed = pointed});

    return getOrInsertType(table, &type).s;
}

const Type *makeOptionalType(TypeTable *table, const Type *target, u64 flags)
{
    target = flattenWrappedType(target, &flags);
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

const Type *makeAliasType(TypeTable *table,
                          const Type *aliased,
                          cstring name,
                          u64 flags)
{
    aliased = flattenWrappedType(aliased, &flags);
    Type type = make(Type,
                     .tag = typAlias,
                     .name = name,
                     .flags = flags,
                     .alias = {.aliased = aliased});

    return getOrInsertType(table, &type).s;
}

const Type *makeOpaqueTypeWithFlags(TypeTable *table,
                                    cstring name,
                                    AstNode *decl,
                                    u64 flags)
{
    Type type = make(Type,
                     .tag = typOpaque,
                     .name = name,
                     .flags = flags,
                     .size = 0,
                     .opaque = {.decl = decl});

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

const Type *changeFunctionRetType(TypeTable *table,
                                  const Type *func,
                                  const Type *ret)
{
    Type type = *func;
    type.func.retType = ret;
    return replaceTypeScoped(table, func, &type);
}

const Type *makeEnum(TypeTable *table, const Type *init)
{
    GetOrInset ret = getOrInsertType(table, init);
    if (!ret.f) {
        Type *tEnum = (Type *)ret.s;
        tEnum->tEnum.options = allocFromMemPool(
            table->memPool, sizeof(EnumOption) * init->tEnum.optionsCount);
        memcpy(tEnum->tEnum.options,
               init->tEnum.options,
               sizeof(EnumOption) * init->tEnum.optionsCount);

        tEnum->tEnum.sortedOptions = allocFromMemPool(
            table->memPool, sizeof(EnumOption *) * init->tEnum.optionsCount);
        for (u64 i = 0; i < init->tEnum.optionsCount; i++)
            tEnum->tEnum.sortedOptions[i] = &tEnum->tEnum.options[i];

        qsort(tEnum->tEnum.sortedOptions,
              tEnum->tEnum.optionsCount,
              sizeof(EnumOption *),
              sortCompareEnumOption);
    }

    return ret.s;
}

const Type *makeGenericType(TypeTable *table, AstNode *decl)
{
    Type type =
        make(Type,
             .tag = typGeneric,
             .name = getDeclarationName(decl),
             .generic = {.decl = decl,
                         .paramsCount = countAstNodes(decl->genericDecl.params),
                         .inferrable = decl->genericDecl.inferrable != 0});
    return getOrInsertType(table, &type).s;
}

const Type *makeWrappedType(TypeTable *table, const Type *target, u64 flags)
{
    if ((target->flags & flags) == flags)
        return target;

    target = flattenWrappedType(target, &flags);
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

    if (flags) {
        *flags = tmp | type->flags;
        *flags &= flgTypeApplicable;
    }

    return type;
}

const Type *flattenWrappedType(const Type *type, u64 *flags)
{
    u64 tmp = flgNone;

    while (typeIs(type, Wrapped)) {
        tmp |= type->flags;
        type = type->wrapped.target;
    }

    if (flags)
        *flags |= tmp;
    return type;
}

GetOrInset makeAppliedType(TypeTable *table, const Type *init)
{
    GetOrInset ret = getOrInsertType(table, init);
    if (!ret.f) {
        Type *applied = (Type *)ret.s;
        applied->applied.args = allocFromMemPool(
            table->memPool, sizeof(Type) * init->applied.totalArgsCount);
        memcpy(applied->applied.args,
               init->applied.args,
               sizeof(Type) * init->applied.totalArgsCount);
    }

    return ret;
}

const Type *makeDestructorType(TypeTable *table)
{
    return table->destructorType
               ?: makeFuncType(
                      table,
                      &(Type){.flags = flgBuiltin | flgFunctionPtr,
                              .name = "Destructor",
                              .tag = typFunc,
                              .func = {.params =
                                           (const Type *[]){makeVoidPointerType(
                                               table, flgNone)},
                                       .paramsCount = 1,
                                       .retType = makeVoidType(table)}});
}

const Type *makeStructType(TypeTable *table,
                           cstring name,
                           NamedTypeMember *members,
                           u64 membersCount,
                           AstNode *decl,
                           const Type *base,
                           const Type **interfaces,
                           u64 interfacesCount,
                           u64 flags)
{
    GetOrInset ret = getOrInsertType(
        table, &(Type){.tag = typStruct, .name = name, .flags = flags});

    if (!ret.f) {
        Type *type = (Type *)ret.s;
        type->tStruct.members =
            makeTypeMembersContainer(table, members, membersCount);
        type->tStruct.inheritance =
            makeTypeInheritance(table, base, interfaces, interfacesCount);
        type->tStruct.decl = decl;
    }

    return ret.s;
}

const Type *makeClassType(TypeTable *table,
                          cstring name,
                          NamedTypeMember *members,
                          u64 membersCount,
                          AstNode *decl,
                          const Type *base,
                          const Type **interfaces,
                          u64 interfacesCount,
                          u64 flags)
{
    GetOrInset ret = getOrInsertType(
        table, &(Type){.tag = typClass, .name = name, .flags = flags});

    if (!ret.f) {
        Type *type = (Type *)ret.s;
        type->tClass.members =
            makeTypeMembersContainer(table, members, membersCount);
        type->tClass.inheritance =
            makeTypeInheritance(table, base, interfaces, interfacesCount);
        type->tClass.decl = decl;
    }

    return ret.s;
}

const Type *replaceStructType(TypeTable *table,
                              const Type *og,
                              NamedTypeMember *members,
                              u64 membersCount,
                              AstNode *decl,
                              const Type *base,
                              const Type **interfaces,
                              u64 interfacesCount,
                              u64 flags)
{
    removeFromTypeTable(table, og);
    return makeStructType(table,
                          og->name,
                          members,
                          membersCount,
                          decl,
                          base,
                          interfaces,
                          interfacesCount,
                          flags);
}

const Type *replaceClassType(TypeTable *table,
                             const Type *og,
                             NamedTypeMember *members,
                             u64 membersCount,
                             AstNode *decl,
                             const Type *base,
                             const Type **interfaces,
                             u64 interfacesCount,
                             u64 flags)
{
    removeFromTypeTable(table, og);
    return makeClassType(table,
                         og->name,
                         members,
                         membersCount,
                         decl,
                         base,
                         interfaces,
                         interfacesCount,
                         flags);
}

const Type *makeInterfaceType(TypeTable *table,
                              cstring name,
                              NamedTypeMember *members,
                              u64 membersCount,
                              AstNode *decl,
                              u64 flags)
{
    GetOrInset ret = getOrInsertType(
        table, &(Type){.tag = typInterface, .name = name, .flags = flags});
    if (!ret.f) {
        Type *interface = (Type *)ret.s;
        interface->tInterface.members =
            makeTypeMembersContainer(table, members, membersCount);
        interface->tInterface.decl = decl;
    }

    return ret.s;
}

const Type *makeModuleType(TypeTable *table,
                           cstring name,
                           cstring path,
                           NamedTypeMember *members,
                           u64 membersCount)
{
    Type type = make(Type,
                     .tag = typModule,
                     .name = name,
                     .flags = flgNone,
                     .module = {.path = path});
    GetOrInset ret = getOrInsertType(table, &type);
    if (!ret.f) {
        Type *module = (Type *)ret.s;
        module->module.members =
            makeTypeMembersContainer(table, members, membersCount);
    }

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

    if ((typeIs(_left, String) || typeIs(_left, Pointer) ||
         typeIs(_left, Opaque)) &&
        typeIs(stripPointer(_right), Null))
        return left;

    if (typeIs(_left, Enum))
        _left = _left->tEnum.base;
    if (typeIs(_right, Enum))
        _right = _right->tEnum.base;

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
    case typPointer:
        return isIntegralType(_right) ? _left : NULL;
    default:
        return NULL;
    }
}

const Type *findMemberInType(const Type *type, cstring name)
{
    const Type *found = NULL;
    switch (type->tag) {
    case typThis:
        found = findMemberInType(type->this.that, name);
        break;
    case typStruct:
        found = findStructMemberType(type, name);
        break;
    case typClass:
        found = findClassMemberType(type, name);
        break;
    case typInterface:
        found = findInterfaceMemberType(type, name);
        break;
    case typEnum:
        found = findEnumOptionType(type, name);
        break;
    case typModule:
        found = findModuleMemberType(type, name);
        break;
    case typWrapped:
        found = findMemberInType(unwrapType(type, NULL), name);
        break;
    default:
        break;
    }

    return found;
}

const Type *expectInType(TypeTable *table,
                         const Type *type,
                         Log *L,
                         cstring name,
                         const FileLoc *loc)
{
    const Type *found = findMemberInType(type, name);
    if (L && found == NULL) {
        logError(L,
                 loc,
                 "'{s}' is undefined in '{s}'",
                 (FormatArg[]){{.s = name}, {.s = type->name}});
        return makeErrorType(table);
    }
    return found;
}

const Type *getIntegerTypeForLiteral(TypeTable *table, i64 literal)
{
    if (literal >= INT8_MIN && literal <= INT8_MAX)
        return getPrimitiveType(table, prtI8);
    else if (literal >= INT16_MIN && literal <= INT16_MAX)
        return getPrimitiveType(table, prtI16);
    else if (literal >= INT32_MIN && literal <= INT32_MAX)
        return getPrimitiveType(table, prtI32);
    else
        return getPrimitiveType(table, prtI64);
}

bool isIntegerTypeInRange(const Type *type, i64 min, i64 max)
{
    csAssert0(min <= max);
    const IntMinMax minMax = getIntegerTypeMinMax(type);
    return min <= (i64)minMax.s && max >= minMax.f;
}

int findTypeInArray(const Type **types, u64 count, const Type *type)
{
    for (int i = 0; i < count; i++) {
        if (compareTypes(types[i], type))
            return i;
    }

    return -1;
}

AstNode *getTypeDecl(const Type *type)
{
    if (type == NULL)
        return NULL;

    switch (type->tag) {
    case typStruct:
        return type->tStruct.decl;
    case typClass:
        return type->tClass.decl;
    case typFunc:
        return type->func.decl;
    case typThis:
        return getTypeDecl(type->this.that);
    case typEnum:
        return type->tEnum.decl;
    case typGeneric:
        return type->generic.decl;
    case typInterface:
        return type->tInterface.decl;
    default:
        return NULL;
    }
}
