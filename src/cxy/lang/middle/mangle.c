//
// Created by Carter Mbotho on 2024-04-30.
//
#include "mangle.h"

#include <core/strpool.h>

#include <lang/frontend/flag.h>
#include <lang/frontend/ttable.h>

static void mangleType(FormatState *state, const Type *type)
{
    u64 flags = flgNone;
    type = unwrapType(type, &flags);
    if (flags & flgConst) {
        append(state, "c", 1);
    }
    type = resolveAndUnThisType(type);

    switch (type->tag) {
    case typPrimitive:
        switch (type->primitive.id) {
#define ff(TAG, ...) case prt##TAG:
            INTEGER_TYPE_LIST(ff)
            FLOAT_TYPE_LIST(ff)
            appendString(state, type->name);
            break;
#undef ff
        default:
            append(state, type->name, 1);
            break;
        }
        break;
    case typString:
        append(state, "s", 1);
        break;
    case typArray:
        append(state, "A", 1);
        format(state, "{u64}", (FormatArg[]){{.u64 = type->array.len}});
        mangleType(state, type->array.elementType);
        break;
    case typTuple:
        append(state, "T", 1);
        for (u64 i = 0; i < type->tuple.count; i++) {
            mangleType(state, type->tuple.members[i]);
        }
        append(state, "_", 1);
        break;
    case typFunc:
        append(state, "F", 1);
        for (u64 i = 0; i < type->func.paramsCount; i++) {
            mangleType(state, type->func.params[i]);
        }
        mangleType(state, type->func.retType);
        append(state, "_", 1);
        break;
    case typPointer:
        append(state, "P", 1);
        mangleType(state, type->pointer.pointed);
        break;
    default:
        append(state, "Z", 1);
        if (type->ns)
            appendString(state, type->ns);
        if (type->name != NULL) {
            appendString(state, type->name);
        }
        append(state, "_", 1);
        break;
    }
}

cstring makeMangledName(
    StrPool *strings, cstring name, const Type **types, u64 count, bool isConst)
{
    FormatState state = newFormatState(NULL, true);
    appendString(&state, name);
    if (isConst)
        append(&state, "Ic_", 3);
    else
        append(&state, "I_", 2);

    for (u64 i = 0; i < count; i++) {
        mangleType(&state, types[i]);
    }
    append(&state, "E", 1);
    char *str = formatStateToString(&state);
    cstring mangled = makeString(strings, str);
    free(str);
    freeFormatState(&state);
    return mangled;
}