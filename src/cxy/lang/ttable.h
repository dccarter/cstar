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
const Type *makeArrayType(TypeTable *table, const Type *elementType, u64 size);
const Type *makeMapType(TypeTable *table, const Type *key, const Type *value);
const Type *makeAliasType(TypeTable *table, const Type *aliased);
const Type *makeUnionType(TypeTable *table, const Type **members, u64 count);
const Type *makeTupleType(TypeTable *table, const Type **members, u64 count);
const Type *makeFuncType(TypeTable *table,
                         bool isVariadic,
                         const Type *retType,
                         const Type **params,
                         u64 paramsCount);
