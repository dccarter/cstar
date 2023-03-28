//
// Created by Carter on 2023-03-11.
//

#pragma once

#include <core/utils.h>

#include <stdbool.h>

/*
 * Front-end types, including a simple module system based on M. Lillibridge's
 * translucent sums, and HM-style polymorphism. Types should always be created
 * via a `TypeTable` object.
 */

// clang-format off

#define INTEGER_TYPE_LIST(f) \
    f(I8,   "i8")         \
    f(I16,  "i16")        \
    f(I32,  "i32")        \
    f(I64,  "i64")        \
    f(U8,   "u8")         \
    f(U16,  "u16")        \
    f(U32,  "u32")        \
    f(U64,  "u64")        \

#define PRIM_TYPE_LIST(f) \
    f(Bool, "bool")       \
    f(Char, "char")       \
    INTEGER_TYPE_LIST(f)  \
    f(F32,  "f32")        \
    f(F64,  "f64")

typedef enum {
#define f(name, ...) prt##name,
    PRIM_TYPE_LIST(f)
#undef f
    prtCOUNT
} PrtId;

// clang-format on

typedef enum {
    typNull,
    typVoid,
    typAuto,
    typBool,
    typChar,
    typInt,
    typFloat,
    typString,
    typEnum,
    typStruct,
    typUnion,
    typArray,
    typAlias,
    typTuple,
    typFunc,
    typPointer
} TypeTag;

typedef struct Type Type;
typedef struct AstNode AstNode;

typedef struct StructField {
    const char *name;
    const Type *type;
    bool hasDefault : 1;
} StructField;

typedef struct EnumOption {
    const char *name;
    u64 value;
} EnumOption;

typedef struct FuncParam {
    const char *name;
    const Type *type;
} FuncParam;

struct Type {
    u64 id;
    TypeTag tag;
    u64 size;
    union {
        struct {
            PrtId id;
        } number;

        struct {
            const Type *aliased;
        } alias;

        struct {
            const Type **members;
            u64 memberCount;
        } tUnion;

        struct {
            const Type **members;
            u64 count;
        } tuple;

        struct {
            const Type *elementType;
            u64 count;
        } array;

        struct {
            const char *name;
            const StructField **fields;
            u64 fieldsCount;
        } tStruct;

        struct {
            const char *name;
            const AstNode *decl;
            u64 count;
            bool isVariadic;
        } func;
    };
};

typedef struct TypeTable TypeTable;

const Type *makeErrorType(TypeTable *table);
const Type *makePrimitiveType(TypeTable *table, PrtId id);
const Type *makeNullType(TypeTable *table);
const Type *makeAutoType(TypeTable *table);
const Type *makeVoidType(TypeTable *table);
const Type *makeStringType(TypeTable *table);
const Type *makeTupleType(TypeTable *table, const Type **member, u64 count);
const Type *makeArrayType(TypeTable *table,
                          const Type *elementsType,
                          u64 count);
const Type *makeUnknownArrayType(TypeTable *table);
const Type *makeAnonymousStructType(TypeTable *table,
                                    const StructField *fields,
                                    u64 fieldsCount);

const Type *areTypesCompatible(TypeTable *table,
                               const Type *target,
                               const Type *from);
const Type *getMemberType(TypeTable *table,
                          const Type *target,
                          const char *name);

const Type *getElementType(TypeTable *table, const Type *target, u64 index);

const Type *isIndexOperationSupported(TypeTable *table,
                                      const Type *target,
                                      const Type **indices,
                                      u64 indicesCount);
const Type *findClosestUnionType(TypeTable *table,
                                 const Type *unionType,
                                 const Type *anonymous);

const bool isErrorType(TypeTable *table, const Type *type);