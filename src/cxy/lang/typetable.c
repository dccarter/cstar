//
// Created by Carter on 2023-03-22.
//

#include "typetable.h"
#include "ast.h"

#include <core/htable.h>
#include <core/mempool.h>
#include <core/strpool.h>

typedef struct TypeTable {
    HashTable types;
    MemPool *memPool;
    StrPool strPool;
    u64 typeCount;
    const Type *unknownType;
    const Type *voidType;
    const Type *nullType;
    const Type *primitiveTypes[prtCOUNT];
} TypeTable;

static HashCode hasTypes(HashCode hash, const Type **types, u64 count)
{
    for (u64 i = 0; i < count; i++)
        hash = hashUint64(hash, types[i]->id);
    return hash;
}

static HashCode hashType(HashCode hash, const Type *type)
{
    hash = hashUint32(hash, type->tag);
    switch (type->tag) {
    case typChar:
    case typBool:
    case typAuto:
    case typNull:
    case typVoid:
        break;

    case typInt:
    case typFloat:
        hash = hashUint32(hash, type->number.id);
        break;
    case typAlias:
        hash = hashType(hash, type->alias.aliased);
        break;
    }

    return hash;
}

const Type *resolveType(TypeTable *table, const Type *type)
{
    while (true) {
        switch (type->tag) {
        case typAlias:
            type = resolveType(table, type->alias.aliased);
            break;
        default:
            return type;
        }
    }
}
