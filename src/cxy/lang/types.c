/**
 * Credits:
 */

#include "types.h"
#include "ast.h"
#include "ttable.h"

#include "token.h"

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

bool isTypeAssignableFrom(const Type *to, const Type *from)
{
    if (to == from) {
        return true;
    }

    to = resolveType(to);
    from = resolveType(from);

    if (to->tag == typPointer && from->tag == typPointer) {
        if (to->pointer.pointed->tag == typVoid)
            return true;
        if (typeIs(from->pointer.pointed, Null))
            return true;

        return isTypeAssignableFrom(to->pointer.pointed, from->pointer.pointed);
    }

    if (to->tag == typOptional && from->tag == typOptional) {
        return isTypeAssignableFrom(to->optional.target, from->optional.target);
    }

    switch (to->tag) {
    case typAuto:
        return from->tag != typError;
    case typPrimitive:
        if (from->tag == typEnum) {
            return isTypeAssignableFrom(to, from->tEnum.base);
        }

        switch (to->primitive.id) {
#define f(I, ...) case prt##I:
            SIGNED_INTEGER_TYPE_LIST(f)
#undef f
            return isIntegerType(from) && isSignedType(from) &&
                   to->size >= from->size;
#define f(I, ...) case prt##I:
            UNSIGNED_INTEGER_TYPE_LIST(f)
#undef f
            return isIntegerType(from) && isUnsignedType(from) &&
                   to->size >= from->size;
#define f(I, ...) case prt##I:
            FLOAT_TYPE_LIST(f)
#undef f
            return isFloatType(from) || isIntegerType(from);
        default:
            return to->primitive.id == from->primitive.id;
        }
    case typPointer:
        if (from->tag == typArray)
            return isTypeAssignableFrom(to->pointer.pointed,
                                        from->array.elementType);
        if (typeIs(to->pointer.pointed, Void) && typeIs(from, Pointer))
            return true;
        return false;

    case typArray:
        if (!typeIs(from, Array) ||
            !isTypeAssignableFrom(to->array.elementType,
                                  from->array.elementType))
            return false;
        if (to->size == UINT64_MAX)
            return true;
        return to->size == from->size;

    case typOptional:
        return from->tag == typNull ||
               isTypeAssignableFrom(to->optional.target, from);
    case typThis:
        return to->this.that == from;
    case typTuple:
        if (from->tag != typTuple || to->tuple.count != from->tuple.count)
            return false;
        for (u64 i = 0; i < from->tuple.count; i++) {
            if (!isTypeAssignableFrom(to->tuple.members[i],
                                      from->tuple.members[i]))
                return false;
        }
        return true;
    case typFunc: {
        if (!isTypeAssignableFrom(to->func.retType, to->func.retType))
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
        return from->tag == typEnum &&
               isTypeAssignableFrom(to->tEnum.base, from->tEnum.base);
    case typStruct:
        return typeIs(from, This) && to == from->this.that;
    default:
        return false;
    }
}

bool isTypeCastAssignable(const Type *to, const Type *from)
{
    to = resolveType(to);
    from = resolveType(from);

    if (to->tag == from->tag) {
        if (to->tag != typPrimitive)
            return true;
    }

    if (from->tag == typAuto)
        return to;

    switch (to->tag) {
    case typAuto:
        return false;
    case typPrimitive:
        switch (to->primitive.id) {
#define f(I, ...) case prt##I:
            INTEGER_TYPE_LIST(f)
            FLOAT_TYPE_LIST(f)
            return isNumericType(from);
#undef f
        case prtChar:
            return isUnsignedType(from) && from->size <= 4;
        case prtBool:
            return from->primitive.id == prtBool;
        default:
            return to->primitive.id == from->primitive.id;
        }

    default:
        return false;
    }
}

bool isIntegerType(const Type *type)
{
    type = resolveType(type);

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

bool isSignedType(const Type *type)
{
    type = resolveType(type);

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
    if (type->tag != typPrimitive || type->primitive.id == prtBool)
        return false;
    return true;
}

void printType(FormatState *state, const Type *type)
{
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
    case typArray:
        format(state, "[", NULL);
        printType(state, type->array.elementType);
        if (type->array.size != UINT64_MAX)
            format(state, ", {u64}", (FormatArg[]){{.u64 = type->array.size}});
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
    case typGeneric:
        printGenericType(state, type);
        break;
    case typApplied:
        printType(state, type->applied.generated);
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
        format(state, "where )", NULL);
        break;
    default:
        unreachable("TODO");
    }
}
