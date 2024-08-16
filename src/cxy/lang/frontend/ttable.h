//
// Created by Carter on 2023-03-28.
//

#pragma once

#pragma once

#include "core/log.h"
#include "core/mempool.h"
#include "core/strpool.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TypeTable {
    HashTable types;
    MemPool *memPool;
    StrPool *strPool;
    cstring currentNamespace;
    u64 typeCount;
    const Type *autoType;
    const Type *voidType;
    const Type *_nullType;
    const Type *nullType;
    const Type *errorType;
    const Type *stringType;
    const Type *anySliceType;
    const Type *primitiveTypes[prtCOUNT];
    const Type *destructorType;
    const Type *optionalType;
} TypeTable;

typedef CxyPair(bool, const Type *) GetOrInset;

TypeTable *newTypeTable(MemPool *pool, StrPool *strPool);

void freeTypeTable(TypeTable *table);

const Type *resolveType(const Type *type);

const Type *resolveAndUnThisType(const Type *type);

const Type *stripPointer(const Type *type);

const Type *stripReference(const Type *type);

const Type *stripPointerOrReference(const Type *type);

const Type *stripAll(const Type *type);

const Type *stripOnce(const Type *type, u64 *flags);

const Type *arrayToPointer(TypeTable *table, const Type *type);

const Type *getPrimitiveType(TypeTable *table, PrtId id);

const Type *getAnySliceType(TypeTable *table);

const Type *makeErrorType(TypeTable *table);

const Type *makeAutoType(TypeTable *table);

const Type *makeVoidType(TypeTable *table);

const Type *makeNullType(TypeTable *table);

const Type *makeStringType(TypeTable *table);

const Type *makePointerType(TypeTable *table, const Type *pointed, u64 flags);

const Type *makeReferenceType(TypeTable *table,
                              const Type *referred,
                              u64 flags);

const Type *makeOptionalType(TypeTable *table, const Type *target, u64 flags);

const Type *makeTypeInfo(TypeTable *table, const Type *target);

const Type *makeArrayType(TypeTable *table, const Type *elementType, u64 size);

const Type *makeContainerType(TypeTable *table,
                              cstring name,
                              const Type *base,
                              cstring *names,
                              u64 count);

static inline const Type *makeVoidPointerType(TypeTable *table, u64 flags)
{
    return makePointerType(table, makeVoidType(table), flags);
}

const Type *makeMapType(TypeTable *table, const Type *key, const Type *value);

const Type *makeAliasType(TypeTable *table,
                          const Type *aliased,
                          cstring name,
                          u64 flags);

const Type *makeOpaqueTypeWithFlags(TypeTable *table,
                                    cstring name,
                                    AstNode *decl,
                                    u64 flags);

static inline const Type *makeOpaqueType(TypeTable *table,
                                         cstring name,
                                         AstNode *decl)
{
    return makeOpaqueTypeWithFlags(table, name, decl, 0);
}

const Type *makeUnionType(TypeTable *table,
                          UnionMember *members,
                          u64 count,
                          u64 flags);
const Type *makeUntaggedUnionType(TypeTable *table,
                                  AstNode *decl,
                                  NamedTypeMember *members,
                                  u64 count);
const Type *makeReplaceUntaggedUnionType(TypeTable *table,
                                         AstNode *decl,
                                         NamedTypeMember *members,
                                         u64 count);

const Type *makeTupleType(TypeTable *table,
                          const Type **members,
                          u64 count,
                          u64 flags);

const Type *makeThisType(TypeTable *table, cstring name, u64 flags);

const Type *makeFuncType(TypeTable *table, const Type *init);

const Type *changeFunctionRetType(TypeTable *table,
                                  const Type *func,
                                  const Type *ret);

const Type *makeStructType(TypeTable *table,
                           cstring name,
                           NamedTypeMember *members,
                           u64 memberCount,
                           AstNode *decl,
                           u64 flags);
const Type *makeReplaceStructType(TypeTable *table,
                                  cstring name,
                                  NamedTypeMember *members,
                                  u64 memberCount,
                                  AstNode *decl,
                                  u64 flags);

const Type *findStructType(TypeTable *table, cstring name, u64 flags);
const Type *findUntaggedUnionType(TypeTable *table, cstring name, u64 flags);
const Type *findEnumType(TypeTable *table, cstring name, u64 flags);

const Type *makeClassType(TypeTable *table,
                          cstring name,
                          NamedTypeMember *members,
                          u64 memberCount,
                          AstNode *decl,
                          const Type *base,
                          const Type **interfaces,
                          u64 interfacesCount,
                          u64 flags);

const Type *replaceStructType(TypeTable *table,
                              const Type *og,
                              NamedTypeMember *members,
                              u64 membersCount,
                              AstNode *decl,
                              u64 flags);

const Type *replaceAliasType(TypeTable *table,
                             const Type *og,
                             const Type *aliased,
                             u64 flags);

const Type *replaceClassType(TypeTable *table,
                             const Type *og,
                             NamedTypeMember *members,
                             u64 membersCount,
                             AstNode *decl,
                             const Type *base,
                             const Type **interfaces,
                             u64 interfacesCount,
                             u64 flags);

const Type *makeInterfaceType(TypeTable *table,
                              cstring name,
                              NamedTypeMember *members,
                              u64 memberCount,
                              AstNode *decl,
                              u64 flags);

const Type *makeModuleType(TypeTable *table,
                           cstring name,
                           cstring path,
                           NamedTypeMember *members,
                           u64 memberCount,
                           u64 flags);

const Type *makeEnum(TypeTable *table, const Type *init);

const Type *makeGenericType(TypeTable *table, AstNode *decl);

const Type *makeWrappedType(TypeTable *table, const Type *target, u64 flags);

const Type *unwrapType(const Type *type, u64 *flags);

const Type *flattenWrappedType(const Type *type, u64 *flags);

GetOrInset makeAppliedType(TypeTable *table, const Type *init);

const Type *makeDestructorType(TypeTable *table);

u64 getTypesCount(TypeTable *table);

u64 sortedByInsertionOrder(TypeTable *table, const Type **types, u64 size);

void enumerateTypeTable(TypeTable *table,
                        void *ctx,
                        bool(with)(void *, const void *));

const Type *promoteType(TypeTable *table, const Type *left, const Type *right);

const Type *getBuiltinOptionalType(Log *L);

const Type *findMemberInType(const Type *type, cstring name);

const Type *expectInType(TypeTable *table,
                         const Type *type,
                         Log *L,
                         cstring name,
                         const FileLoc *loc);

void buildModuleType(TypeTable *types, AstNode *node, bool isBuiltinModule);

const Type *getIntegerTypeForLiteral(TypeTable *table, i64 literal);

bool isIntegerTypeInRange(const Type *type, i64 min, i64 max);

int findTypeInArray(const Type **types, u64 count, const Type *type);

u64 pointerLevels(const Type *type);

const Type *removeFromTypeTable(TypeTable *table, const Type *type);

AstNode *getTypeDecl(const Type *type);

#ifdef __cplusplus
}
#endif
