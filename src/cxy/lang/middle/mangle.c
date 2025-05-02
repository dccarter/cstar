//
// Created by Carter Mbotho on 2024-04-30.
//
#include "mangle.h"

#include <core/strpool.h>

#include <lang/frontend/flag.h>
#include <lang/frontend/ttable.h>

static void mangleAstNode(FormatState *state, const AstNode *node, u64 idx)
{
    switch (node->tag) {
    case astNullLit:
        format(state, "Ln_", NULL);
        break;
    case astBoolLit:
        format(state, "Lb_{b}", (FormatArg[]){{.b = node->boolLiteral.value}});
        break;
    case astCharLit:
        format(state, "Lc_{c}", (FormatArg[]){{.c = node->charLiteral.value}});
        break;
    case astIntegerLit:
        if (node->intLiteral.isNegative)
            format(state,
                   "Li_{i64}",
                   (FormatArg[]){{.i64 = node->intLiteral.value}});
        else
            format(state,
                   "Lu_{u64}",
                   (FormatArg[]){{.u64 = node->intLiteral.uValue}});
        break;
    case astFloatLit:
        format(state, "Lf_{u64}", (FormatArg[]){{.c = idx}});
        break;
    case astStringLit:
        format(state, "Ls_{u64}", (FormatArg[]){{.c = idx}});
        break;
    default:
        unreachable("TODO");
    }
}

static void mangleType(FormatState *state, const Type *type)
{
    u64 flags = flgNone;
    type = unwrapType(type, &flags);
    if (flags & flgConst) {
        append(state, "c", 1);
    }
    type = resolveType(type);

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
    case typUnion:
        if (type->tUnion.count > 3) {
            format(state, "U{u64}", (FormatArg[]){{.u64 = type->index}});
        }
        else {
            append(state, "U_", 2);
            for (u64 i = 0; i < type->tUnion.count; i++) {
                mangleType(state, type->tUnion.members[i].type);
            }
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
    case typReference:
        append(state, "R", 1);
        mangleType(state, type->reference.referred);
        break;
    case typLiteral:
        mangleAstNode(state, type->literal.value, type->index);
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