/**
 * Credits:
 */

#include "types.h"
#include "ast.h"
#include "flag.h"
#include "strings.h"
#include "ttable.h"

#include "lang/middle/builtins.h"

#include <string.h>

#include "token.h"

static int searchCompareStructMember(const void *lhs, const void *rhs)
{
    const NamedTypeMember *right = *((const NamedTypeMember **)rhs),
                          *left = lhs;
    return left->name == right->name ? 0 : strcmp(left->name, right->name);
}

static int searchCompareEnumOption(const void *lhs, const void *rhs)
{
    const EnumOptionDecl *right = *((const EnumOptionDecl **)rhs), *left = lhs;
    return left->name == right->name ? 0 : strcmp(left->name, right->name);
}

static void printManyTypes(FormatState *state,
                           const Type **types,
                           u64 count,
                           cstring sep,
                           bool keyword)
{
    for (int i = 0; i < count; i++) {
        if (i != 0)
            format(state, "{s}", (FormatArg[]){{.s = sep}});
        printType_(state, types[i], keyword);
    }
}

static void printNamedType(FormatState *state, const Type *type)
{
    if (type->ns)
        format(state, "{s}.", (FormatArg[]){{.s = type->ns}});
    if (type->name) {
        format(state, "{s}", (FormatArg[]){{.s = type->name}});
    }
}

static void printGenericType(FormatState *state, const Type *type)
{
    if (nodeIs(type->generic.decl, StructDecl) ||
        nodeIs(type->generic.decl, ClassDecl)) {
        printKeyword(state, "struct");
        format(
            state,
            " {s}[",
            (FormatArg[]){
                {.s = type->generic.decl->genericDecl.decl->structDecl.name}});
    }
    else if (nodeIs(type->generic.decl, TypeDecl)) {
        printKeyword(state, "type");
        format(state,
               " {s}[",
               (FormatArg[]){
                   {.s = type->generic.decl->genericDecl.decl->typeDecl.name}});
    }
    else {
        printKeyword(state, "func ");
        format(state,
               " {s}[",
               (FormatArg[]){
                   {.s = type->generic.decl->genericDecl.decl->funcDecl.name}});
    }

    for (u64 i = 0; i < type->generic.paramsCount; i++) {
        if (i != 0)
            format(state, ", ", NULL);
        if (type->generic.params)
            format(state, type->generic.params[i].name, NULL);
        else
            format(state, "NULL", NULL);
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

    if (isSliceType(to) && isArrayType(from)) {
        return unwrapType(getSliceTargetType(to), NULL) ==
               unwrapType(from->array.elementType, NULL);
    }

    const Type *_to = unwrapType(to, NULL), *_from = unwrapType(from, NULL);
    if (isTypeConst(from) && !isTypeConst(to)) {
        if (typeIs(_to, Pointer))
            return false;
    }

    to = _to, from = _from;
    if (to == from)
        return true;

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
        if (isVoidPointer(to))
            return typeIs(from, Pointer) || typeIs(from, String) ||
                   typeIs(from, Array) || typeIs(stripAll(from), Class);

        if (typeIs(from, Array))
            return isTypeAssignableFrom(to->pointer.pointed,
                                        from->array.elementType);

        return typeIs(from, Pointer) && isVoidPointer(from);

    case typReference:
        return typeIs(from, Reference) &&
               isTypeAssignableFrom(to->reference.referred,
                                    from->reference.referred);

    case typOpaque:
        return typeIs(from, Pointer) && typeIs(from->pointer.pointed, Null);

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
        if (typeIs(to->_this.that, Alias) && typeIs(from, Pointer))
            return isTypeAssignableFrom(to->_this.that, from->pointer.pointed);
        if (typeIs(to->_this.that, Alias) && typeIs(from, Reference))
            return isTypeAssignableFrom(to->_this.that,
                                        from->reference.referred);
        return isTypeAssignableFrom(to->_this.that, from);
    case typTuple:
        if (!typeIs(from, Tuple) || to->tuple.count != from->tuple.count)
            return false;
        for (u64 i = 0; i < from->tuple.count; i++) {
            if (!isTypeAssignableFrom(to->tuple.members[i],
                                      from->tuple.members[i]))
                return false;
        }
        return true;

    case typUnion:
        for (u64 i = 0; i < to->tUnion.count; i++) {
            if (to->tUnion.members[i].type == from)
                return true;
        }
        return false;

    case typFunc: {
        if (stripPointer(from)->tag == typNull)
            return true;
        if (!typeIs(from, Func))
            return false;
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
        if (typeIs(from, This))
            return to == from->_this.that;
        return typeIs(from, Pointer) && typeIs(from->pointer.pointed, Null);
    case typClass:
        if (typeIs(from, This))
            return to == from->_this.that;
        if (typeIs(from, Class) && from->tClass.inheritance->base)
            return to == from->tClass.inheritance->base;
        if (typeIs(from, Class) && getTypeDecl(to) == getTypeDecl(from))
            // TODO workaround circular types
            return true;

        return typeIs(from, Pointer) && typeIs(from->pointer.pointed, Null);
    case typInfo:
        return typeIs(from, Info) &&
               isTypeAssignableFrom(to->info.target, from->info.target);
    case typInterface:
        return (typeIs(from, Struct) || typeIs(from, Class)) &&
               implementsInterface(from, to);
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
        if (to->primitive.id == unwrappedFrom->primitive.id)
            return true;

        switch (to->primitive.id) {
#define f(I, ...) case prt##I:
            INTEGER_TYPE_LIST(f)
            FLOAT_TYPE_LIST(f)
            return isNumericType(unwrappedFrom);
#undef f
        case prtChar:
            return (isUnsignedType(unwrappedFrom) &&
                    unwrappedFrom->size <= 4) ||
                   (unwrappedFrom->primitive.id == prtCChar);
        case prtCChar:
            return (isUnsignedType(unwrappedFrom) &&
                    unwrappedFrom->size <= 4) ||
                   (unwrappedFrom->primitive.id == prtChar);
        case prtBool:
            return unwrappedFrom->primitive.id == prtBool;
        default:
            return unwrappedTo->primitive.id == unwrappedFrom->primitive.id;
        }
    case typEnum:
        if (isIntegerType(to))
            return isTypeCastAssignable(to, from->tEnum.base);
    case typPointer:
        if (isVoidPointer(unwrappedFrom) &&
            (isClassType(unwrappedTo) || isStructPointer(unwrappedTo)))
            return true;
        return isTypeAssignableFrom(to, from);
    case typThis:
        if (isClassType(unwrappedFrom) && isVoidPointer(unwrappedTo))
            return true;
        else
            return isTypeAssignableFrom(to, from);
    case typUnion:
        return findUnionTypeIndex(from,
                                  typeIs(to, Pointer) ? to->pointer.pointed
                                                      : to) != UINT32_MAX;
    case typReference:
        if (isReferenceType(unwrappedTo))
            return isTypeAssignableFrom(to, from);
        else
            return isTypeAssignableFrom(to, from->reference.referred);
    case typClass:
        if (isVoidPointer(unwrappedTo))
            return true;
        // fallthrough
    default:
        return isTypeAssignableFrom(to, from);
    }
}

bool isPrimitiveTypeBigger(const Type *lhs, const Type *rhs)
{
    lhs = unwrapType(lhs, NULL);
    rhs = unwrapType(rhs, NULL);
    csAssert0(isNumericType(lhs) && isNumericType(rhs));

    if (isFloatType(lhs)) {
        return !isFloatType(rhs) ||
               getPrimitiveTypeSizeFromTag(lhs->primitive.id) >
                   getPrimitiveTypeSizeFromTag(rhs->primitive.id);
    }
    else if (!isFloatType(rhs)) {
        return (getPrimitiveTypeSizeFromTag(lhs->primitive.id) >
                getPrimitiveTypeSizeFromTag(rhs->primitive.id));
    }

    return false;
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

bool isSignedIntegerType(const Type *type)
{
    type = resolveType(type);
    if (typeIs(type, Info))
        return isSignedIntegerType(type->info.target);

    if (typeIs(type, Wrapped))
        return isSignedIntegerType(unwrapType(type, NULL));

    if (!type || type->tag != typPrimitive)
        return false;
    switch (type->primitive.id) {
#define f(I, ...) case prt##I:
        SIGNED_INTEGER_TYPE_LIST(f)
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

bool isSliceType(const Type *type)
{
    return typeIs(type, Struct) && hasFlag(type, Slice);
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

bool isReferenceType(const Type *type)
{
    type = resolveType(type);

    if (typeIs(type, Wrapped))
        return isReferenceType(unwrapType(type, NULL));

    if (typeIs(type, Info))
        return isReferenceType(type->info.target);

    return typeIs(type, Reference);
}

bool isPointerOrReferenceType(const Type *type)
{
    type = resolveType(type);

    if (typeIs(type, Wrapped))
        return isPointerOrReferenceType(unwrapType(type, NULL));

    if (typeIs(type, Info))
        return isPointerOrReferenceType(type->info.target);

    return typeIs(type, Pointer) || typeIs(type, Reference) ||
           typeIs(type, Array) || typeIs(type, String);
}

bool isReferable(const Type *type)
{
    type = unwrapType(resolveType(type), NULL);
    if (type == NULL)
        return false;
    switch (type->tag) {
    case typClass:
    case typStruct:
    case typTuple:
    case typUnion:
    case typThis:
        return !hasFlag(type, Extern);
    default:
        return false;
    }
}

bool isVoidPointer(const Type *type)
{
    type = resolveType(type);

    if (typeIs(type, Wrapped))
        return isVoidPointer(type->wrapped.target);

    return typeIs(type, Pointer) && typeIs(type->pointer.pointed, Void);
}

bool isClassType(const Type *type)
{
    type = unwrapType(resolveType(type), NULL);
    return typeIs(type, Class) ||
           (typeIs(type, This) && typeIs(type->_this.that, Class));
}

bool isClassReferenceType(const Type *type)
{
    return isReferenceType(type) && isClassType(stripReference(type));
}

bool isStructType(const Type *type)
{
    type = unwrapType(type, NULL);
    //    if (typeIs(type, Info))
    //        return isStructType(type->info.target);
    return typeIs(type, Struct) ||
           (typeIs(type, This) && typeIs(type->_this.that, Struct));
}

bool isUnionType(const Type *type)
{
    type = unwrapType(type, NULL);
    return typeIs(type, Union);
}

bool isTupleType(const Type *type)
{
    type = unwrapType(type, NULL);
    return typeIs(type, Tuple);
}

bool isConstType(const Type *type)
{
    u64 flags = flgNone;
    type = unwrapType(type, &flags);
    return flags & flgConst;
}

bool typeIsBaseOf(const Type *base, const Type *type)
{
    const Type *it = resolveAndUnThisType(type);
    while (typeIs(it, Class) && it->tClass.inheritance->base) {
        if (it->tClass.inheritance->base == base)
            return true;
        it = resolveAndUnThisType(it->tClass.inheritance->base);
    }
    return false;
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

void printType_(FormatState *state, const Type *type, bool keyword)
{
    u64 flags = flgNone;
    type = unwrapType(type, &flags);
    if (flags & flgConst) {
        printKeyword(state, "const ");
    }

    if (type->from != NULL) {
        if (typeIs(type, Func))
            printKeyword(state, "func ");
        printType_(state, type->from, keyword);
        if (typeIs(type, Func)) {
            format(state, "(", NULL);
            printManyTypes(state,
                           type->func.params,
                           type->func.paramsCount,
                           ", ",
                           keyword);
            format(state, ") -> ", NULL);
            printType_(state, type->func.retType, keyword);
        }
        return;
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
        printWithStyle(state, "|error|", errorStyle);
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
        format(state, "^", NULL);
        printType_(state, type->pointer.pointed, keyword);
        break;
    case typReference:
        format(state, "&", NULL);
        printType_(state, type->reference.referred, keyword);
        break;
    case typOptional:
        printType_(state, type->optional.target, keyword);
        format(state, "?", NULL);
        break;
    case typArray:
        format(state, "[", NULL);
        printType_(state, type->array.elementType, keyword);
        if (type->array.len != UINT64_MAX)
            format(state, ", {u64}", (FormatArg[]){{.u64 = type->array.len}});
        format(state, "]", NULL);
        break;
    case typMap:
        format(state, "{[", NULL);
        printType_(state, type->map.key, keyword);
        format(state, "]: ", NULL);
        printType_(state, type->map.value, keyword);
        format(state, "}", NULL);
        break;
    case typAlias:
    case typOpaque:
        if (keyword)
            printKeyword(state, "type ");
        printNamedType(state, type);
        break;
    case typTuple:
        format(state, "(", NULL);
        printManyTypes(
            state, type->tuple.members, type->tuple.count, ", ", keyword);
        format(state, ")", NULL);
        break;
    case typUnion:
        for (u64 i = 0; i < type->tUnion.count; i++) {
            if (i != 0)
                format(state, " | ", NULL);
            printType_(state, type->tUnion.members[i].type, keyword);
        }
        break;
    case typUntaggedUnion:
        format(state, "@[tagged] ", NULL);
        printKeyword(state, "union ");
        printNamedType(state, type);
        break;
    case typFunc:
        printKeyword(state, "func");
        format(state, "(", NULL);
        printManyTypes(
            state, type->func.params, type->func.paramsCount, ", ", keyword);
        format(state, ") -> ", NULL);
        printType_(state, type->func.retType, keyword);
        break;
    case typThis:
        if (type->_this.that != NULL)
            printType_(state, type->_this.that, keyword);
        else
            format(state, "Unresolved", NULL);
        break;
    case typEnum:
        if (keyword)
            printKeyword(state, "enum ");
        printNamedType(state, type);
        break;
    case typStruct:
        if (keyword)
            printKeyword(state, "struct ");
        printNamedType(state, type);
        break;
    case typClass:
        if (keyword)
            printKeyword(state, "class ");
        printNamedType(state, type);
        break;
    case typInterface:
        if (keyword)
            printKeyword(state, "interface ");
        printNamedType(state, type);
        break;
    case typGeneric:
        printGenericType(state, type);
        break;
    case typApplied:
        if (type->applied.from->ns)
            format(state, "{s}.", (FormatArg[]){{.s = type->applied.from->ns}});
        format(state, "{s}[", (FormatArg[]){{.s = type->applied.from->name}});

        for (u64 i = 0; i < type->applied.argsCount; i++) {
            if (i != 0)
                format(state, ", ", NULL);
            printType_(state, type->applied.args[i], keyword);
        }
        format(state, "]", NULL);
        break;
    case typInfo:
        format(state, "#", NULL);
        printType_(state, type->info.target, keyword);
        break;

    case typModule:
        if (keyword)
            printKeyword(state, "module");
        format(state, " {s}", (FormatArg[]){{.s = type->name}});
        break;

    case typWrapped: {
        Type tmp = *type->wrapped.target;
        tmp.flags |= type->flags;
        printType_(state, &tmp, keyword);
        break;
    }
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

u64 getPrimitiveTypeSizeFromTag(PrtId tag)
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

IntMinMax getIntegerTypeMinMax(const Type *type)
{
    type = unwrapType(type, NULL);
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

int sortCompareStructMember(const void *lhs, const void *rhs)
{
    const NamedTypeMember *left = *((const NamedTypeMember **)lhs),
                          *right = *((const NamedTypeMember **)rhs);
    return left->name == right->name ? 0 : strcmp(left->name, right->name);
}

TypeMembersContainer *makeTypeMembersContainer(TypeTable *types,
                                               const NamedTypeMember *members,
                                               u64 count)
{
    TypeMembersContainer *container =
        allocFromMemPool(types->memPool, sizeof(TypeMembersContainer));
    container->count = count;
    container->members =
        allocFromMemPool(types->memPool, sizeof(container->members[0]) * count);
    memcpy(container->members, members, sizeof(container->members[0]) * count);
    container->sortedMembers = allocFromMemPool(
        types->memPool, sizeof(container->sortedMembers[0]) * count);

    for (u64 i = 0; i < count; i++)
        container->sortedMembers[i] = &container->members[i];

    qsort(container->sortedMembers,
          container->count,
          sizeof(container->sortedMembers[0]),
          sortCompareStructMember);

    return container;
}

TypeInheritance *makeTypeInheritance(TypeTable *types,
                                     const Type *base,
                                     const Type **interfaces,
                                     u64 interfaceCount)
{
    TypeInheritance *inheritance =
        allocFromMemPool(types->memPool, sizeof(TypeInheritance));
    inheritance->interfacesCount = interfaceCount;
    inheritance->base = base;
    if (interfaceCount) {
        inheritance->interfaces = allocFromMemPool(
            types->memPool,
            sizeof(inheritance->interfaces[0]) * interfaceCount);
        memcpy(inheritance->interfaces,
               interfaces,
               sizeof(inheritance->interfaces[0]) * interfaceCount);
    }
    else
        inheritance->interfaces = NULL;

    return inheritance;
}

const NamedTypeMember *findNamedTypeMemberInContainer(
    const TypeMembersContainer *container, cstring member)
{
    int index = binarySearch(container->sortedMembers,
                             container->count,
                             &(NamedTypeMember){.name = member},
                             sizeof(NamedTypeMember *),
                             searchCompareStructMember);

    return index == -1 ? NULL : container->sortedMembers[index];
}

const NamedTypeMember *findOverloadMemberUpInheritanceChain(const Type *type,
                                                            cstring member)
{
    while (type != NULL && isClassOrStructType(type)) {
        const NamedTypeMember *named = findStructMember(type, member);
        if (named)
            return named;
        type = getTypeBase(type);
    }
    return NULL;
}

const TypeInheritance *getTypeInheritance(const Type *type)
{
    type = unThisType(type);
    switch (type->tag) {
    case typClass:
        return type->tClass.inheritance;
    default:
        return NULL;
    }
}

AstNode *findMemberDeclInType(const Type *type, cstring name)
{
    const NamedTypeMember *member;
    switch (type->tag) {
    case typThis:
        return findMemberDeclInType(type->_this.that, name);
    case typStruct:
        member = findStructMember(type, name);
        return member ? (AstNode *)member->decl : NULL;
    case typInterface:
        member = findInterfaceMember(type, name);
        return member ? (AstNode *)member->decl : NULL;
    case typModule:
        member = findModuleMember(type, name);
        return member ? (AstNode *)member->decl : NULL;
    case typClass:
        member = findClassMember(type, name);
        return member ? (AstNode *)member->decl : NULL;
    case typUntaggedUnion:
        member = findUntaggedUnionMember(type, name);
        return member ? (AstNode *)member->decl : NULL;
    case typEnum: {
        const EnumOptionDecl *option = findEnumOption(type, name);
        return option ? (AstNode *)option->decl : NULL;
    }
    default:
        return NULL;
    }
}

const Type *getTypeBase(const Type *type)
{
    type = stripReference(type);
    switch (type->tag) {
    case typClass:
        return type->tClass.inheritance ? type->tClass.inheritance->base : NULL;
    case typThis:
        return getTypeBase(type->_this.that);
    case typEnum:
        return type->tEnum.base;
    default:
        return NULL;
    }
}

bool implementsInterface(const Type *type, const Type *inf)
{
    const TypeInheritance *inheritance = getTypeInheritance(type);
    if (inheritance == NULL)
        return false;

    for (u64 i = 0; i < inheritance->interfacesCount; i++) {
        if (inheritance->interfaces[i] == inf)
            return true;
    }

    return false;
}

const EnumOptionDecl *findEnumOption(const Type *type, cstring option)
{
    int index = binarySearch(type->tEnum.sortedOptions,
                             type->tEnum.optionsCount,
                             &(EnumOptionDecl){.name = option},
                             sizeof(NamedTypeMember *),
                             searchCompareEnumOption);

    return index == -1 ? NULL : type->tEnum.sortedOptions[index];
}

bool isTruthyType(const Type *type)
{
    return isIntegralType(type) || isFloatType(type) || typeIs(type, Pointer) ||
           typeIs(type, Optional) ||
           (isClassOrStructType(type) &&
            findStructMemberType(type, S_Truthy) != NULL);
}

const Type *getPointedType(const Type *type)
{
    csAssert0(typeIs(type, Pointer));
    const Type *pointed = type->pointer.pointed;
    return typeIs(pointed, This) ? pointed->_this.that : pointed;
}

const Type *getOptionalTargetType(const Type *type)
{
    if (!hasFlag(type, Optional))
        return NULL;

    return type->tStruct.decl->structDecl.typeParams->type;
}

const Type *getSliceTargetType(const Type *type)
{
    if (!hasFlag(type, Slice))
        return NULL;
    const Type *target = type->tStruct.members->members[0].type;
    csAssert0(typeIs(target, Pointer));
    return target->pointer.pointed;
}

u32 findUnionTypeIndex(const Type *tagged, const Type *type)
{
    if (!typeIs(tagged, Union))
        return UINT32_MAX;
    for (u32 i = 0; i < tagged->tUnion.count; i++) {
        if (tagged->tUnion.members[i].type == type)
            return i;
    }
    return UINT32_MAX;
}

bool hasReferenceMembers(const Type *type)
{
    if (hasFlag(type, ReferenceMembers))
        return true;
    if (isClassOrStructType(type)) {
        AstNode *decl = getTypeDecl(type);
        return hasFlag(decl, ReferenceMembers);
    }

    return isTupleType(type) && hasFlag(type, ReferenceMembers);
}

void pushThisReference(const Type *this, AstNode *node)
{
    if (!typeIs(this, This))
        return;
    Type *type = (Type *)this;
    if (type->_this.references.elems == NULL)
        type->_this.references = newDynArray(sizeof(AstNode *));

    DynArray *refs = &type->_this.references;
    pushOnDynArrayExplicit(refs, &node, sizeof(AstNode *));
}

void resolveThisReferences(TypeTable *table, const Type *this, const Type *type)
{
    if (!typeIs(this, This) || this->_this.references.elems == NULL)
        return;

    DynArray *refs = (DynArray *)&this->_this.references;

    if (type != NULL) {
        for (int i = 0; i < refs->size; i++) {
            AstNode *node = dynArrayAt(AstNode **, refs, i);
            u64 flags = flgNone;
            if (!typeIs(node->type, This))
                unwrapType(node->type, &flags);
            node->type = makePointerType(table, type, flags);
        }
    }

    freeDynArray(refs);
    memset(&((Type *)this)->_this.references, 0, sizeof(DynArray));
    ((Type *)this)->_this.trackReferences = false;
}
