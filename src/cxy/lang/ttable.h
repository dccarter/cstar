//
// Created by Carter on 2023-03-28.
//

#pragma once

#pragma once

#include <core/mempool.h>
#include <core/strpool.h>
#include <lang/types.h>

typedef struct TypeTable TypeTable;
typedef Pair(bool, const Type *) GetOrInset;

TypeTable *newTypeTable(MemPool *pool, StrPool *strPool);
void freeTypeTable(TypeTable *table);

const Type *resolveType(const Type *type);
const Type *stripPointer(const Type *type);
const Type *arrayToPointer(TypeTable *table, const Type *type);
const Type *getPrimitiveType(TypeTable *table, PrtId id);

void removeFromTypeTable(TypeTable *table, const Type *type);
const Type *makeErrorType(TypeTable *table);
const Type *makeAutoType(TypeTable *table);
const Type *makeVoidType(TypeTable *table);
const Type *makeNullType(TypeTable *table);
const Type *makeStringType(TypeTable *table);
const Type *makePointerType(TypeTable *table, const Type *pointed, u64 flags);
const Type *makeOptionalType(TypeTable *table, const Type *target, u64 flags);
const Type *makeArrayType(TypeTable *table, const Type *elementType, u64 size);

static inline const Type *makeVoidPointerType(TypeTable *table, u64 flags)
{
    return makePointerType(table, makeVoidType(table), flags);
}

const Type *makeMapType(TypeTable *table, const Type *key, const Type *value);

const Type *makeAliasType(TypeTable *table, const Type *aliased, cstring name);

const Type *makeOpaqueType(TypeTable *table, cstring name);

const Type *makeUnionType(TypeTable *table, const Type **members, u64 count);

const Type *makeTupleType(TypeTable *table,
                          const Type **members,
                          u64 count,
                          u64 flags);

const Type *makeThisType(TypeTable *table, cstring name, u64 flags);

const Type *makeFuncType(TypeTable *table, const Type *init);

const Type *makeStruct(TypeTable *table, const Type *init);

const Type *makeEnum(TypeTable *table, const Type *init);

const Type *makeGenericType(TypeTable *table, const Type *init);

GetOrInset makeAppliedType(TypeTable *table, const Type *init);

u64 getTypesCount(TypeTable *table);

u64 sortedByInsertionOrder(TypeTable *table, const Type **types, u64 size);

void enumerateTypeTable(TypeTable *table,
                        void *ctx,
                        bool(with)(void *, const void *));

const Type *promoteType(TypeTable *table, const Type *left, const Type *right);
