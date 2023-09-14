/**
 * Credits:
 */

#include "types.h"
#include "ast.h"
#include "builtins.h"
#include "flag.h"
#include "strings.h"
#include "ttable.h"

#include <string.h>

#include "token.h"

static int searchCompareStructMember(const void *lhs, const void *rhs)
{
    const StructMember *right = *((const StructMember **)rhs), *left = lhs;
    return left->name == right->name ? 0 : strcmp(left->name, right->name);
}

static int searchCompareEnumOption(const void *lhs, const void *rhs)
{
    const EnumOption *right = *((const EnumOption **)rhs), *left = lhs;
    return left->name == right->name ? 0 : strcmp(left->name, right->name);
}

static void printManyTypes(FormatState *state,
                           const Type **types,
                           u64 count,
                           cstring sep)
{
    for (int i = 0; i < count; i++) {
        if (i != 0)
            format(state, "{s}", (FormatArg[]){{.s = sep}});
        printType(state, types[i]);
    }
}

static void printGenericType(FormatState *state, const Type *type)
{
    if (nodeIs(type->generic.decl, StructDecl)) {
        printKeyword(state, "struct");
        format(state,
               " {s}[",
               (FormatArg[]){{.s = type->generic.decl->structDecl.name}});
    }
    else if (nodeIs(type->generic.decl, TypeDecl)) {
        printKeyword(state, "type");
        format(state,
               " {s}[",
               (FormatArg[]){{.s = type->generic.decl->typeDecl.name}});
    }
    else {
        printKeyword(state, "func ");
        format(state,
               " {s}[",
               (FormatArg[]){{.s = type->generic.decl->funcDecl.name}});
    }

    for (u64 i = 0; i < type->generic.paramsCount; i++) {
        if (i != 0)
            format(state, ", ", NULL);
        format(state, type->generic.params[i].name, NULL);
    }
    format(state, "]", NULL);
}

bool isPrimitiveType(TokenTag tag)
{
    switch (tag) {
#define f(name, ...) case tok##name:
        PRIM_TYPE_LIST(f)
#undef f
        return true;
    default:
        return false;
    }
}

PrtId tokenToPrimitiveTypeId(TokenTag tag)
{
    switch (tag) {
#define f(name, ...)                                                           \
    case tok##name:                                                            \
        return prt##name;
        PRIM_TYPE_LIST(f)
#undef f
    default:
        return false;
    }
}

bool isTypeConst(const Type *type)
{
    u64 flags = flgNone;
    unwrapType(type, &flags);
    return flags & flgConst;
}

bool isTypeAssignableFrom(const Type *to, const Type *from)
{
    to = resolveType(to);
    from = resolveType(from);
    if (to == from)
        return true;
    if (hasFlag(to, Optional) && !hasFlag(from, Optional)) {
        if (typeIs(from, Pointer) && typeIs(from->pointer.pointed, Null))
            return true;

        to = getOptionalTargetType(to);
    }

    const Type *_to = unwrapType(to, NULL), *_from = unwrapType(from, NULL);
    if (isTypeConst(from) && !isTypeConst(to)) {
        if (typeIs(_to, Pointer))
            return false;
    }

    to = _to, from = _from;

    if (typeIs(to, Pointer) && typeIs(from, Pointer)) {
        if (typeIs(to->pointer.pointed, Void))
            return true;
        if (typeIs(from->pointer.pointed, Null))
            return true;

        return isTypeAssignableFrom(to->pointer.pointed, from->pointer.pointed);
    }

    switch (to->tag) {
    case typAuto:
        return !typeIs(from, Error);
    case typString:
        return typeIs(from, String) || typeIs(stripPointer(from), Null);
    case typPrimitive:
        if (typeIs(from, Enum)) {
            return isTypeAssignableFrom(to, from->tEnum.base);
        }

        switch (to->primitive.id) {
#define f(I, ...) case prt##I:
            SIGNED_INTEGER_TYPE_LIST(f)
#undef f
            if (isIntegerType(from)) {
                if (isUnsignedType(from))
                    return to->size > from->size;
                return to->size >= from->size;
            }
            return false;
#define f(I, ...) case prt##I:
            UNSIGNED_INTEGER_TYPE_LIST(f)
#undef f
            return isIntegerType(from) && to->size >= from->size;
#define f(I, ...) case prt##I:
            FLOAT_TYPE_LIST(f)
#undef f
            return isFloatType(from) || isIntegerType(from);
        case prtChar:
        case prtCChar:
            return isCharacterType(from);

        default:
            return to->primitive.id == from->primitive.id;
        }
    case typPointer:
        if (typeIs(to->pointer.pointed, Void))
            return typeIs(from, Pointer) || typeIs(from, String) ||
                   typeIs(from, Array);

        if (typeIs(from, Array))
            return isTypeAssignableFrom(to->pointer.pointed,
                                        from->array.elementType);

        return typeIs(from, Pointer) && typeIs(from->pointer.pointed, Void);

    case typArray:
        if (!typeIs(from, Array) ||
            !isTypeAssignableFrom(to->array.elementType,
                                  from->array.elementType))
            return false;
        if (to->array.len == UINT64_MAX)
            return true;
        return to->array.len == from->array.len;

    case typOptional:
        if (typeIs(from, Optional))
            return isTypeAssignableFrom(to->optional.target,
                                        from->optional.target);
        return stripPointer(from)->tag == typNull ||
               isTypeAssignableFrom(to->optional.target, from);
    case typThis:
        return to->this.that == from;
    case typTuple:
        if (!typeIs(from, Tuple) || to->tuple.count != from->tuple.count)
            return false;
        for (u64 i = 0; i < from->tuple.count; i++) {
            if (!isTypeAssignableFrom(to->tuple.members[i],
                                      from->tuple.members[i]))
                return false;
        }
        return true;

    case typFunc: {
        if (hasFlag(to, FunctionPtr) && stripPointer(from)->tag == typNull)
            return true;

        if (!isTypeAssignableFrom(to->func.retType, from->func.retType))
            return false;
        bool isNameFuncParam =
            (to->flags & flgFuncTypeParam) && !(from->flags & flgClosure);
        u64 count = to->func.paramsCount;
        count -= isNameFuncParam;
        if (count != from->func.paramsCount) {
            return false;
        }
        for (u64 i = 0; i < count; i++) {
            if (!isTypeAssignableFrom(to->func.params[i + isNameFuncParam],
                                      from->func.params[i]))
                return false;
        }
        return true;
    }
    case typEnum:
        return typeIs(from, Enum) &&
               isTypeAssignableFrom(to->tEnum.base, from->tEnum.base);
    case typStruct:
        return typeIs(from, This) && to == from->this.that;
    case typInfo:
        return typeIs(from, Info) &&
               isTypeAssignableFrom(to->info.target, from->info.target);
    case typInterface:
        return typeIs(from, Struct) && implementsInterface(from, to);
    default:
        return false;
    }
}

bool isTypeCastAssignable(const Type *to, const Type *from)
{
    to = resolveType(to);
    from = resolveType(from);
    u64 toFlags = flgNone, fromFlags = flgNone;
    const Type *unwrappedTo = unwrapType(to, &toFlags);
    const Type *unwrappedFrom = unwrapType(from, &fromFlags);

    if (hasFlag(unwrappedTo, Optional) && !hasFlag(unwrappedFrom, Optional)) {
        if (typeIs(unwrappedFrom, Pointer) &&
            typeIs(unwrappedFrom->pointer.pointed, Null))
            return true;

        to = getOptionalTargetType(unwrappedTo);
    }

    if (unwrappedTo->tag == unwrappedFrom->tag) {
        if (to->tag != typPrimitive)
            return (fromFlags & flgConst) ? (toFlags & flgConst) : true;
    }

    if (unwrappedFrom->tag == typAuto)
        return true;

    switch (unwrappedFrom->tag) {
    case typAuto:
        return false;
    case typPrimitive:
        if (typeIs(to, Optional))
            to = to->optional.target;
        if (!typeIs(to, Primitive))
            return false;

        switch (to->primitive.id) {
#define f(I, ...) case prt##I:
            INTEGER_TYPE_LIST(f)
            FLOAT_TYPE_LIST(f)
            return isNumericType(unwrappedFrom);
#undef f
        case prtChar:
            return isUnsignedType(unwrappedFrom) && unwrappedFrom->size <= 4;
        case prtBool:
            return unwrappedFrom->primitive.id == prtBool;
        default:
            return unwrappedTo->primitive.id == unwrappedFrom->primitive.id;
        }
    default:
        return isTypeAssignableFrom(to, from);
    }
}

bool isIntegerType(const Type *type)
{
    type = resolveType(type);

    if (typeIs(type, Info))
        return isIntegerType(type->info.target);

    if (typeIs(type, Wrapped))
        return isIntegerType(unwrapType(type, NULL));

    if (!type || type->tag != typPrimitive)
        return false;
    switch (type->primitive.id) {
#define f(I, ...) case prt##I:
        INTEGER_TYPE_LIST(f)
        return true;
#undef f
    default:
        return false;
    }
}

bool isIntegralType(const Type *type)
{
    type = resolveType(type);

    if (typeIs(type, Info))
        return isIntegralType(type->info.target);

    if (typeIs(type, Wrapped))
        return isIntegralType(unwrapType(type, NULL));

    if (typeIs(type, Enum))
        return true;

    if (!typeIs(type, Primitive))
        return false;

    switch (type->primitive.id) {
#define f(I, ...) case prt##I:
        INTEGER_TYPE_LIST(f)
    case prtBool:
    case prtChar:
    case prtCChar:
        return true;
#undef f
    default:
        return false;
    }
}

bool isSignedType(const Type *type)
{
    type = resolveType(type);
    if (typeIs(type, Info))
        return isSignedType(type->info.target);

    if (typeIs(type, Wrapped))
        return isSignedType(unwrapType(type, NULL));

    if (!type || type->tag != typPrimitive)
        return false;
    switch (type->primitive.id) {
#define f(I, ...) case prt##I:
        SIGNED_INTEGER_TYPE_LIST(f)
        FLOAT_TYPE_LIST(f)
        return true;
#undef f
    default:
        return false;
    }
}

bool isUnsignedType(const Type *type)
{
    type = resolveType(type);
    if (typeIs(type, Info))
        return isUnsignedType(type->info.target);

    if (typeIs(type, Wrapped))
        return isUnsignedType(unwrapType(type, NULL));

    if (!type || type->tag != typPrimitive)
        return false;
    switch (type->primitive.id) {
#define f(I, ...) case prt##I:
        UNSIGNED_INTEGER_TYPE_LIST(f)
        return true;
#undef f
    default:
        return false;
    }
}

bool isFloatType(const Type *type)
{
    type = resolveType(type);

    if (typeIs(type, Info))
        return isFloatType(type->info.target);

    if (typeIs(type, Wrapped))
        return isFloatType(unwrapType(type, NULL));

    if (!type || type->tag != typPrimitive)
        return false;
    switch (type->primitive.id) {
#define f(I, ...) case prt##I:
        FLOAT_TYPE_LIST(f)
        return true;
#undef f
    default:
        return false;
    }
}

bool isNumericType(const Type *type)
{
    type = resolveType(type);

    if (typeIs(type, Wrapped))
        return isNumericType(unwrapType(type, NULL));

    if (typeIs(type, Info))
        return isNumericType(type->info.target);

    if (typeIs(type, Enum))
        return true;

    if (type == NULL || type->tag != typPrimitive ||
        type->primitive.id == prtBool)
        return false;
    return true;
}

bool isBooleanType(const Type *type)
{
    type = resolveType(type);

    if (typeIs(type, Wrapped))
        return isBooleanType(unwrapType(type, NULL));

    if (typeIs(type, Info))
        return isBooleanType(type->info.target);

    return (type && type->tag == typPrimitive && type->primitive.id == prtBool);
}

bool isCharacterType(const Type *type)
{
    type = resolveType(type);

    if (typeIs(type, Wrapped))
        return isCharacterType(unwrapType(type, NULL));

    if (typeIs(type, Info))
        return isCharacterType(type->info.target);

    return typeIs(type, Primitive) &&
           (type->primitive.id == prtChar || type->primitive.id == prtCChar);
}

bool isArrayType(const Type *type)
{
    type = resolveType(type);

    if (typeIs(type, Wrapped))
        return isArrayType(unwrapType(type, NULL));

    if (typeIs(type, Info))
        return isArrayType(type->info.target);

    return typeIs(type, Array);
}

bool isPointerType(const Type *type)
{
    type = resolveType(type);

    if (typeIs(type, Wrapped))
        return isPointerType(unwrapType(type, NULL));

    if (typeIs(type, Info))
        return isPointerType(type->info.target);

    return typeIs(type, Pointer) || typeIs(type, Array) || typeIs(type, String);
}

bool isBuiltinType(const Type *type)
{
    if (type == NULL)
        return false;

    switch (type->tag) {
    case typPrimitive:
    case typVoid:
    case typString:
    case typAuto:
    case typNull:
        return true;
    case typInfo:
        return isBuiltinType(type->info.target);
    case typWrapped:
        return isBuiltinType(unwrapType(type, NULL));
    default:
        return false;
    }
}

void printType(FormatState *state, const Type *type)
{
    if (type->flags & flgConst) {
        printKeyword(state, "const ");
    }

    switch (type->tag) {
    case typPrimitive:
        switch (type->primitive.id) {
#define f(I, str, ...)                                                         \
    case prt##I:                                                               \
        printKeyword(state, str);                                              \
        return;
            PRIM_TYPE_LIST(f)
#undef f
        default:
            unreachable("");
        }
    case typError:
        printWithStyle(state, "<error>", errorStyle);
        break;
    case typVoid:
        printKeyword(state, "void");
        break;
    case typAuto:
        printKeyword(state, "auto");
        break;
    case typNull:
        printKeyword(state, "null");
        break;
    case typString:
        printKeyword(state, "string");
        break;
    case typPointer:
        format(state, "&", NULL);
        printType(state, type->pointer.pointed);
        break;
    case typOptional:
        printType(state, type->optional.target);
        format(state, "", NULL);
        break;
    case typArray:
        format(state, "[", NULL);
        printType(state, type->array.elementType);
        if (type->array.len != UINT64_MAX)
            format(state, ", {u64}", (FormatArg[]){{.u64 = type->array.len}});
        format(state, "]", NULL);
        break;
    case typMap:
        format(state, "{[", NULL);
        printType(state, type->map.key);
        format(state, "]: ", NULL);
        printType(state, type->map.value);
        format(state, "}", NULL);
        break;
    case typAlias:
    case typUnion:
    case typOpaque:
        printKeyword(state, "type");
        format(state, " {s}", (FormatArg[]){{.s = type->name}});
        break;
    case typTuple:
        format(state, "(", NULL);
        printManyTypes(state, type->tuple.members, type->tuple.count, ", ");
        format(state, ")", NULL);
        break;
    case typFunc:
        printKeyword(state, "func");
        format(state, "(", NULL);
        printManyTypes(state, type->func.params, type->func.paramsCount, ", ");
        format(state, ") -> ", NULL);
        printType(state, type->func.retType);
        break;
    case typThis:
        printKeyword(state, "This");
        break;
    case typEnum:
        printKeyword(state, "enum");
        if (type->name) {
            format(state, " {s}", (FormatArg[]){{.s = type->name}});
        }
        break;
    case typStruct:
        printKeyword(state, "struct");
        if (type->name) {
            format(state, " {s}", (FormatArg[]){{.s = type->name}});
        }
        break;
    case typInterface:
        printKeyword(state, "interface");
        if (type->name) {
            format(state, " {s}", (FormatArg[]){{.s = type->name}});
        }
        break;
    case typGeneric:
        printGenericType(state, type);
        break;
    case typApplied:
        if (type->applied.decl)
            printType(state, type->applied.decl->type);
        format(state, " aka ", NULL);
        printType(state, type->applied.from);
        format(state, "where (", NULL);
        for (u64 i = 0; i < type->applied.argsCount; i++) {
            if (i != 0)
                format(state, ", ", NULL);

            format(state,
                   "{s} = ",
                   (FormatArg[]){
                       {.s = type->applied.from->generic.params[i].name}});
            printType(state, type->applied.args[i]);
        }
        format(state, " )", NULL);
        break;
    case typInfo:
        format(state, "@typeof(", NULL);
        printType(state, type->info.target);
        format(state, ")", NULL);
        break;

    case typModule:
        printKeyword(state, "module");
        format(state, " {s}", (FormatArg[]){{.s = type->name}});
        break;

    case typWrapped:
        printType(state, unwrapType(type, NULL));
        break;
    default:
        unreachable("TODO");
    }
}

const char *getPrimitiveTypeName(PrtId tag)
{
    switch (tag) {
#define f(name, str, ...)                                                      \
    case prt##name:                                                            \
        return str;
        PRIM_TYPE_LIST(f)
#undef f
    default:
        csAssert0(false);
    }
}

u64 getPrimitiveTypeSize(PrtId tag)
{
    switch (tag) {
#define f(name, str, size)                                                     \
    case prt##name:                                                            \
        return size;
        PRIM_TYPE_LIST(f)
#undef f
    default:
        csAssert0(false);
    }
}

const IntMinMax getIntegerTypeMinMax(const Type *type)
{
    static IntMinMax minMaxTable[] = {
        [prtBool] = {.f = false, .s = true},
        [prtCChar] = {.f = 0, .s = 255},
        [prtChar] = {.f = 0, .s = UINT32_MAX},
        [prtI8] = {.f = INT8_MIN, .s = INT8_MAX},
        [prtU8] = {.f = 0, .s = UINT8_MAX},
        [prtI16] = {.f = INT16_MIN, .s = INT16_MAX},
        [prtU16] = {.f = 0, .s = UINT16_MAX},
        [prtI32] = {.f = INT32_MIN, .s = INT32_MAX},
        [prtU32] = {.f = 0, .s = UINT32_MAX},
        [prtI64] = {.f = INT64_MIN, .s = INT64_MAX},
        [prtU64] = {.f = 0, .s = UINT64_MAX},
    };

    csAssert0(isIntegralType(type));
    if (typeIs(type, Enum)) {
        csAssert0(type->tEnum.optionsCount);
        return (IntMinMax){
            .f = type->tEnum.options[0].value,
            type->tEnum.options[type->tEnum.optionsCount - 1].value};
    }
    return minMaxTable[type->primitive.id];
}

const StructMember *findStructMember(const Type *type, cstring member)
{
    int index = binarySearch(type->tStruct.sortedMembers,
                             type->tStruct.membersCount,
                             &(StructMember){.name = member},
                             sizeof(StructMember *),
                             searchCompareStructMember);

    return index == -1 ? NULL : type->tStruct.sortedMembers[index];
}

const StructMember *findInterfaceMember(const Type *type, cstring member)
{
    int index = binarySearch(type->tInterface.sortedMembers,
                             type->tInterface.membersCount,
                             &(StructMember){.name = member},
                             sizeof(StructMember *),
                             searchCompareStructMember);

    return index == -1 ? NULL : type->tInterface.sortedMembers[index];
}

bool implementsInterface(const Type *type, const Type *inf)
{
    for (u64 i = 0; i < type->tStruct.interfacesCount; i++) {
        if (type->tStruct.interfaces[i] == inf)
            return true;
    }

    return false;
}

const EnumOption *findEnumOption(const Type *type, cstring option)
{
    int index = binarySearch(type->tEnum.sortedOptions,
                             type->tEnum.optionsCount,
                             &(EnumOption){.name = option},
                             sizeof(StructMember *),
                             searchCompareEnumOption);

    return index == -1 ? NULL : type->tEnum.sortedOptions[index];
}

const ModuleMember *findModuleMember(const Type *type, cstring member)
{
    int index = binarySearch(type->module.sortedMembers,
                             type->module.membersCount,
                             &(ModuleMember){.name = member},
                             sizeof(ModuleMember *),
                             searchCompareStructMember);

    return index == -1 ? NULL : type->module.sortedMembers[index];
}

bool isTruthyType(const Type *type)
{
    return isIntegralType(type) || isFloatType(type) || typeIs(type, Pointer) ||
           typeIs(type, Optional) ||
           (typeIs(type, Struct) &&
            findStructMemberType(type, S_Truthy) != NULL);
}

const Type *getOptionalType()
{
    static const Type *optionalType = NULL;
    if (optionalType == NULL)
        optionalType = findBuiltinType(S_Optional);
    return optionalType;
}

const Type *getOptionalTargetType(const Type *type)
{
    return hasFlag(type, Optional) ? type->tStruct.members[1].type : NULL;
}