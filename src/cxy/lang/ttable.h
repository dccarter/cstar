//
// Created by Carter on 2023-03-28.
//

#pragma once

#pragma once

#include <core/mempool.h>
#include <lang/types.h>

typedef struct TypeTable TypeTable;

TypeTable *newTypeTable(MemPool *pool);
void freeTypeTable(TypeTable *table);

const Type *resolveType(TypeTable *table, const Type *type);

const Type *makeErrorType(TypeTable *table);
const Type *makeAutoType(TypeTable *table);
const Type *makeVoidType(TypeTable *table);
const Type *makeNullType(TypeTable *table);
const Type *makePrimitiveType(TypeTable *table, PrtId id);
const Type *makeStringType(TypeTable *table);
const Type *makePointerType(TypeTable *table,
                            const Type *pointed,
                            bool isConst);
const Type *makeArrayType(TypeTable *table,
                          const Type *elementType,
                          const u64 *indexes,
                          u64 indexesCount);

const Type *makeMapType(TypeTable *table, const Type *key, const Type *value);
const Type *makeAliasType(TypeTable *table, const Type *aliased, cstring name);
const Type *makeUnionType(TypeTable *table, const Type **members, u64 count);
const Type *makeTupleType(TypeTable *table, const Type **members, u64 count);
const Type *makeFuncType(TypeTable *table,
                         cstring name,
                         bool isVariadic,
                         const Type *retType,
                         const Type **params,
                         u64 paramsCount);

u64 getTypesCount(TypeTable *table);
u64 sortedByInsertionOrder(TypeTable *table, const Type **types, u64 size);

void enumerateTypeTable(TypeTable *table,
                        void *ctx,
                        bool(with)(void *, const void *));
