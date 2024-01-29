//
// Created by Carter on 2023-03-11.
//

#pragma once

#include "core/array.h"
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
    typClass,
    typInterface,
    typGeneric,
    typApplied,
    typWrapped
} TTag;

typedef struct Type Type;
typedef struct TypeTable TypeTable;
typedef struct Env Env;
typedef struct AstNode AstNode;

typedef struct NamedTypeMember {
    const char *name;
    const Type *type;
    const AstNode *decl;
} NamedTypeMember;

typedef struct TypeMemberContainer {
    NamedTypeMember *members;
    NamedTypeMember **sortedMembers;
    u64 count;
} TypeMembersContainer;

typedef struct TypeInheritance {
    const Type *base;
    const Type **interfaces;
    u64 interfacesCount;
} TypeInheritance;

typedef struct GenericParam {
    const char *name;
    u32 inferIndex;
} GenericParam;

typedef struct EnumOption {
    const char *name;
    i64 value;
    AstNode *decl;
} EnumOption;

typedef struct UnionMember {
    const Type *type;
    void *codegen;
} UnionMember;

typedef struct AppliedTypeParams {
    const Type **params;
    u64 count;
} AppliedTypeParams;

#define CXY_TYPE_HEAD                                                          \
    TTag tag;                                                                  \
    u64 size;                                                                  \
    u64 index;                                                                 \
    u64 flags;                                                                 \
    cstring name;                                                              \
    cstring ns;                                                                \
    const Type *from;                                                          \
    void *codegen;

#ifdef __cpluplus

#endif

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
            u8 _[1];
        } _body;

        struct {
            PrtId id;
        } primitive;

        struct {
            const Type *pointed;
        } pointer;

        struct {
            bool trackReferences;
            DynArray references;
            const Type *that;
        } _this;

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
            AstNode *decl;
        } alias;

        struct {
            AstNode *decl;
        } opaque;

        struct {
            const Type *target;
        } optional, info, wrapped;

        struct {
            u64 count;
            UnionMember *members;
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
            TypeMembersContainer *members;
            cstring path;
        } module;

        struct {
            const Type *base;
            EnumOption *options;
            EnumOption **sortedOptions;
            u64 optionsCount;
            AstNode *decl;
        } tEnum;

        struct {
            TypeInheritance *inheritance;
            TypeMembersContainer *members;
            AstNode *decl;
        } tStruct;

        struct {
            TypeInheritance *inheritance;
            TypeMembersContainer *members;
            AstNode *decl;
        } tClass;

        struct {
            TypeMembersContainer *members;
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
bool isPrimitiveTypeBigger(const Type *to, const Type *from);
bool isIntegerType(const Type *type);
bool isIntegralType(const Type *type);
bool isSignedType(const Type *type);
bool isSignedIntegerType(const Type *type);
bool isUnsignedType(const Type *type);
bool isFloatType(const Type *type);
bool isNumericType(const Type *type);
bool isBuiltinType(const Type *type);
bool isBooleanType(const Type *type);
bool isCharacterType(const Type *type);
bool isArrayType(const Type *type);
bool isPointerType(const Type *type);
bool isVoidPointer(const Type *type);
bool isClassType(const Type *type);
bool isStructType(const Type *type);
bool isTupleType(const Type *type);
bool isConstType(const Type *type);

bool hasReferenceMembers(const Type *type);

static inline bool isClassOrStructType(const Type *type)
{
    return isClassType(type) || isStructType(type);
}

static inline bool isStructPointer(const Type *type)
{
    switch (type->tag) {
    case typPointer:
        return typeIs(type->pointer.pointed, Struct) ||
               isStructPointer(type->pointer.pointed);
    case typThis:
        return typeIs(type->_this.that, Struct);
    default:
        return false;
    }
}

const char *getPrimitiveTypeName(PrtId tag);
u64 getPrimitiveTypeSizeFromTag(PrtId tag);
static inline u64 getPrimitiveTypeSize(const Type *type)
{
    csAssert0(typeIs_(type, typPrimitive));
    return getPrimitiveTypeSizeFromTag(type->primitive.id);
}
void printType(FormatState *state, const Type *type);

bool isSliceType(const Type *type);

IntMinMax getIntegerTypeMinMax(const Type *id);

static inline const Type *unThisType(const Type *_this)
{
    return typeIs(_this, This) ? _this->_this.that : _this;
}

const NamedTypeMember *findNamedTypeMemberInContainer(
    const TypeMembersContainer *container, cstring member);

static inline const NamedTypeMember *findStructMember(const Type *type,
                                                      cstring member)
{
    return findNamedTypeMemberInContainer(unThisType(type)->tStruct.members,
                                          member);
}

static inline const Type *findStructMemberType(const Type *type, cstring member)
{
    const NamedTypeMember *found = findStructMember(type, member);
    return found ? found->type : NULL;
}

static inline const NamedTypeMember *findClassMember(const Type *type,
                                                     cstring member)
{
    return findNamedTypeMemberInContainer(unThisType(type)->tClass.members,
                                          member);
}

static inline const Type *findClassMemberType(const Type *type, cstring member)
{
    const NamedTypeMember *found = findClassMember(type, member);
    return found ? found->type : NULL;
}

const TypeInheritance *getTypeInheritance(const Type *type);
const Type *getTypeBase(const Type *type);

bool implementsInterface(const Type *type, const Type *inf);

static inline const NamedTypeMember *findInterfaceMember(const Type *type,
                                                         cstring member)
{
    return findNamedTypeMemberInContainer(type->tInterface.members, member);
}

static inline const Type *findInterfaceMemberType(const Type *type,
                                                  cstring member)
{
    const NamedTypeMember *found = findInterfaceMember(type, member);
    return found ? found->type : NULL;
}

const EnumOption *findEnumOption(const Type *type, cstring member);

static inline const Type *findEnumOptionType(const Type *type, cstring member)
{
    const EnumOption *found = findEnumOption(type, member);
    return found ? type : NULL;
}

static inline const NamedTypeMember *findModuleMember(const Type *type,
                                                      cstring member)
{
    return findNamedTypeMemberInContainer(type->module.members, member);
}

static inline const Type *findModuleMemberType(const Type *type, cstring member)
{
    const NamedTypeMember *found = findModuleMember(type, member);
    return found ? found->type : NULL;
}

TypeMembersContainer *makeTypeMembersContainer(TypeTable *types,
                                               const NamedTypeMember *members,
                                               u64 count);

TypeInheritance *makeTypeInheritance(TypeTable *types,
                                     const Type *base,
                                     const Type **interfaces,
                                     u64 interfaceCount);

AstNode *findMemberDeclInType(const Type *type, cstring name);

const Type *getPointedType(const Type *type);
bool isTruthyType(const Type *type);
const Type *getOptionalType();
const Type *getOptionalTargetType(const Type *type);
const Type *getSliceTargetType(const Type *type);
u32 findUnionTypeIndex(const Type *tagged, const Type *type);
void pushThisReference(const Type *_this, AstNode *node);
void resolveThisReferences(TypeTable *table,
                           const Type *_this,
                           const Type *type);
