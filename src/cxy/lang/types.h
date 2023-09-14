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
    typInterface,
    typGeneric,
    typApplied,
    typWrapped
} TTag;

typedef struct Type Type;
typedef struct TypeTable TypeTable;
typedef struct Env Env;
typedef struct AstNode AstNode;

typedef struct StructMember {
    const char *name;
    const Type *type;
    const AstNode *decl;
} StructMember, ModuleMember;

typedef struct GenericParam {
    const char *name;
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
            u16 paramsCount;
            u16 capturedNamesCount;
            u16 defaultValuesCount;
            const Type *retType;
            const Type **params;
            const char **captureNames;
            AstNode *decl;
        } func;

        struct {
            const Type *base;
            cstring *names;
            u64 namesCount;
        } container;

        struct {
            ModuleMember *members;
            ModuleMember **sortedMembers;
            cstring path;
            u32 membersCount;
        } module;

        struct {
            const Type *base;
            EnumOption *options;
            EnumOption **sortedOptions;
            u64 optionsCount;
            AstNode *decl;
        } tEnum;

        struct {
            const Type *base;
            const Type **interfaces;
            StructMember *members;
            StructMember **sortedMembers;
            u32 interfacesCount;
            u32 membersCount;
            AstNode *decl;
            AstNode *generatedFrom;
        } tStruct;

        struct {
            StructMember *members;
            StructMember **sortedMembers;
            u64 membersCount;
            AstNode *decl;
        } tInterface;

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
            const Type *from;
            AstNode *decl;
        } applied;
    };
} Type;

typedef Pair(i64, u64) IntMinMax;

#define CYX_TYPE_BODY_SIZE (sizeof(Type) - sizeof(((Type *)0)->_head))
static inline bool typeIs_(const Type *type, TTag tag)
{
    return type && type->tag == tag;
}

#define typeIs(T, TAG) typeIs_((T), typ##TAG)

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

const char *getPrimitiveTypeName(PrtId tag);
u64 getPrimitiveTypeSize(PrtId tag);

void printType(FormatState *state, const Type *type);

static inline bool isSliceType(const Type *type)
{
    return typeIs(type, Array) && type->array.len == UINT64_MAX;
}

const IntMinMax getIntegerTypeMinMax(const Type *id);

const StructMember *findStructMember(const Type *type, cstring member);

static inline const Type *findStructMemberType(const Type *type, cstring member)
{
    const StructMember *found = findStructMember(type, member);
    return found ? found->type : NULL;
}

bool implementsInterface(const Type *type, const Type *inf);

const StructMember *findInterfaceMember(const Type *type, cstring member);

static inline const Type *findInterfaceMemberType(const Type *type,
                                                  cstring member)
{
    const StructMember *found = findInterfaceMember(type, member);
    return found ? found->type : NULL;
}

const EnumOption *findEnumOption(const Type *type, cstring member);

static inline const Type *findEnumOptionType(const Type *type, cstring member)
{
    const EnumOption *found = findEnumOption(type, member);
    return found ? type : NULL;
}

const ModuleMember *findModuleMember(const Type *type, cstring member);

static inline const Type *findModuleMemberType(const Type *type, cstring member)
{
    const ModuleMember *found = findModuleMember(type, member);
    return found ? type : NULL;
}

bool isTruthyType(const Type *type);
const Type *getOptionalType();
const Type *getOptionalTargetType(const Type *type);
