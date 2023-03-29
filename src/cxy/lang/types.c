/**
 * Credits:
 */

#include "types.h"
#include "ttable.h"

#include "token.h"

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

bool isTypeAssignableFrom(TypeTable *table, const Type *to, const Type *from)
{
    to = resolveType(table, to);
    from = resolveType(table, from);

    if (to->tag == from->tag) {
        if (to->tag != typPrimitive)
            return true;
    }

    switch (to->tag) {
    case typAuto:
        return from->tag != typError;
    case typPrimitive:
        switch (to->primitive.id) {
#define f(I, ...) case prt##I:
            INTEGER_TYPE_LIST(f)
#undef f
            return isIntegerType(table, from) && isSignedType(table, from) &&
                   to->size >= from->size;
#define f(I, ...) case prt##I:
            FLOAT_TYPE_LIST(f)
#undef f
            return isFloatType(table, from);
        default:
            return to->primitive.id == from->primitive.id;
        }

    default:
        return false;
    }
}

bool isIntegerType(TypeTable *table, const Type *type)
{
    type = resolveType(table, type);

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

bool isSignedType(TypeTable *table, const Type *type)
{
    type = resolveType(table, type);

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

bool isUnsignedType(TypeTable *table, const Type *type)
{
    type = resolveType(table, type);

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

bool isFloatType(TypeTable *table, const Type *type)
{
    type = resolveType(table, type);

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
