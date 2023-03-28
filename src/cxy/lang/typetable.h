//
// Created by Carter on 2023-03-22.
//

#pragma once

#include <core/mempool.h>
#include <lang/types.h>

typedef struct TypeTable TypeTable;

TypeTable *newTypeTable(MemPool *pool);
void freeTypeTable(TypeTable *table);

const Type *resolveType(TypeTable *table, const Type *type);
