//
// Created by Carter on 2023-03-11.
//

#pragma once

#include "core/format.h"
#include "core/utils.h"

#include <stdbool.h>

/*
 * Front-end types, including a simple module system based on M. Lillibridge's
 * translucent sums, and HM-style polymorphism. Types should always be created
 * via a `TypeTable` object.
 */

// clang-format off
#define UNSIGNED_INTEGER_TYPE_LIST(f) \
    f(U8,   "u8", 1)         \
    f(U16,  "u16", 2)        \
    f(U32,  "u32", 4)        \
    f(U64,  "u64", 8)        \

#define SIGNED_INTEGER_TYPE_LIST(f) \
    f(I8,   "i8", 1)         \
    f(I16,  "i16", 2)        \
    f(I32,  "i32", 4)        \
    f(I64,  "i64", 8)        \

#define INTEGER_TYPE_LIST(f)        \
    UNSIGNED_INTEGER_TYPE_LIST(f)   \
    SIGNED_INTEGER_TYPE_LIST(f)     \

#define FLOAT_TYPE_LIST(f)          \
    f(F32,  "f32", 4)                  \
    f(F64,  "f64", 8)

#define PRIM_TYPE_LIST(f)  \
    f(Bool, "bool", 1)     \
    f(Char, "wchar", 4)    \
    f(CChar, "char", 1)    \
    INTEGER_TYPE_LIST(f)   \
    FLOAT_TYPE_LIST(f)

typedef enum {
#define f(name, ...) prt##name,
    PRIM_TYPE_LIST(f)
#undef f
    prtCOUNT
} PrtId;

// clang-format on

typedef enum {
    typError,
    typContainer,
    typAuto,
    typVoid,
    typNull,
    typInfo,
    typThis,
    typPrimitive,
    typString,
    typPointer,
    typOptional,
    typArray,
    typMap,
    typAlias,
    typUnion,
    typOpaque,
    typTuple,
    typFunc,
    typEnum,
    typModule,
    typStruct,
    typGeneric,
    typApplied,
    typWrapped
} TTag;

typedef struct Type Type;
typedef struct TypeTable TypeTable;
typedef struct Env Env;
typedef struct AstNode AstNode;

typedef struct StructField {
    const char *name;
    const Type *type;
    const AstNode *decl;
} StructField;

typedef struct GenericParam {
    const char *name;
    const AstNode *decl;
    u32 inferIndex;
} GenericParam;

typedef struct EnumOption {
    const char *name;
    i64 value;
} EnumOption;

#define CXY_TYPE_HEAD                                                          \
    TTag tag;                                                                  \
    u64 size;                                                                  \
    u64 index;                                                                 \
    u64 flags;                                                                 \
    cstring name;                                                              \
    cstring namespace;

typedef struct Type {
    union {
        struct {
            CXY_TYPE_HEAD
        };
        struct {
            CXY_TYPE_HEAD
        } _head;
    };

    union {
        struct {
        } _body;

        struct {
            PrtId id;
        } primitive;

        struct {
            const Type *pointed;
        } pointer;

        struct {
            const Type *that;
        } this;

        struct {
            u64 len;
            const Type *elementType;
        } array;

        struct {
            const Type *key;
            const Type *value;
        } map;

        struct {
            const Type *aliased;
        } alias;

        struct {
            const Type *target;
        } optional, info, wrapped;

        struct {
            u64 count;
            const Type **members;
        } tUnion;

        struct {
            u64 count;
            const Type **members;
        } tuple;

        struct {
            u32 paramsCount;
            u32 capturedNamesCount;
            u32 defaultValuesCount;
            const Type *retType;
            const Type **params;
            const char **captureNames;
            AstNode *decl;
            Env *env;
        } func;

        struct {
            const Type *base;
            Env *env;
        } container;

        struct {
            const Type *base;
            EnumOption *options;
            u64 count;
            AstNode *decl;
            Env *env;
        } tEnum;

        struct {
            const Type *base;
            StructField *fields;
            u64 fieldsCount;
            AstNode *decl;
            Env *env;
        } tStruct;

        struct {
            GenericParam *params;
            u64 paramsCount;
            AstNode *decl;
            bool inferrable;
        } generic;

        struct {
            const Type **args;
            u32 argsCount;
            u64 totalArgsCount;
            const Type *generated;
            const Type *from;
        } applied;
    };
} Type;

#define CYX_TYPE_BODY_SIZE (sizeof(Type) - sizeof(((Type *)0)->_head))
#define typeIs(T, TAG) ((T) && (T)->tag == typ##TAG)

bool isTypeAssignableFrom(const Type *to, const Type *from);
bool isTypeCastAssignable(const Type *to, const Type *from);
bool isIntegerType(const Type *type);
bool isIntegralType(const Type *type);
bool isSignedType(const Type *type);
bool isUnsignedType(const Type *type);
bool isFloatType(const Type *type);
bool isNumericType(const Type *type);
bool isBuiltinType(const Type *type);
bool isBooleanType(const Type *type);
bool isCharacterType(const Type *type);
bool isArrayType(const Type *type);
bool isPointerType(const Type *type);

void printType(FormatState *state, const Type *type);

static inline bool isSliceType(const Type *type)
{
    return typeIs(type, Array) && type->array.len == UINT64_MAX;
}

static inline bool isTruthyType(TypeTable *table, const Type *type)
{
    return isIntegralType(type) || isFloatType(type) || typeIs(type, Pointer);
}
