//
// Created by Carter Mbotho on 2024-01-09.
//

#include "eval.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/ttable.h"
#include "lang/middle/sema/check.h"

void evalPath(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    AstNode *elem = node->path.elements;
    AstNode *symbol = elem->pathElement.resolvesTo;
    if (symbol == NULL) {
        logError(ctx->L,
                 &elem->loc,
                 "reference to undefined compile time symbol '{s}'",
                 (FormatArg[]){
                     {.s = elem->pathElement.alt ?: elem->pathElement.name}});
        node->tag = astError;
        return;
    }

    if (elem->pathElement.args && typeIs(symbol->type, Generic)) {
        const Type *type =
            resolveGenericDecl(ctx->typer, elem->pathElement.resolvesTo, elem);
        if (type == NULL || typeIs(type, Error)) {
            node->tag = astError;
            return;
        }
        symbol = getTypeDecl(type);
    }
    else if (nodeIs(symbol, VarDecl))
        symbol = symbol->varDecl.init;

    if (elem->next) {
        elem = elem->next;
        if (nodeIs(symbol, EnumDecl)) {
            cstring name = elem->pathElement.alt ?: elem->pathElement.name;
            AstNode *option = findEnumOptionByName(symbol, name);
            if (option == NULL) {
                logError(
                    ctx->L,
                    &node->loc,
                    "enum {s} does not have an option named {s}",
                    (FormatArg[]){{.s = symbol->enumDecl.name}, {.s = name}});

                node->tag = astError;
                return;
            }
            symbol = option->enumOption.value;
        }
        else {
            while (elem) {
                cstring name = elem->pathElement.alt ?: elem->pathElement.name;
                symbol = evalAstNodeMemberAccess(ctx, &elem->loc, symbol, name);

                if (symbol == NULL) {
                    logError(ctx->L,
                             &elem->loc,
                             "undefined compile time member named '{s}'",
                             (FormatArg[]){{.s = name}});
                    node->tag = astError;
                    return;
                }
                elem = elem->next;
            }
        }
    }

    if (hasFlag(node, Typeinfo)) {
        if (symbol->type == NULL) {
            node->tag = astError;
            return;
        }
        const Type *type = stripAll(symbol->type);
    retry:
        switch (type->tag) {
        case typPrimitive:
            node->tag = astPrimitiveType;
            node->primitiveType.id = type->primitive.id;
            break;
        case typVoid:
            node->tag = astVoidType;
            break;
        case typString:
            node->tag = astStringType;
            break;
        case typEnum:
        case typStruct:
        case typClass: {
            AstNode *decl =
                typeIs(type, Enum) ? type->tEnum.decl : type->tStruct.decl;
            if (decl == NULL) {
                logError(ctx->L,
                         &node->loc,
                         "should have existed since type exists",
                         NULL);
                node->tag = astError;
                return;
            }

            node->tag = astRef;
            node->reference.target = decl;
            node->type = type;
            break;
        }
        case typPointer:
        case typArray:
            *node = *symbol;
            break;
        case typInfo:
            type = symbol->type->info.target;
            goto retry;
        case typTuple:
        case typUnion:
            node->tag = astTypeRef;
            node->flags = type->flags;
            node->type = type;
            break;
        default:
            csAssert0(false);
        }
    }
    else {
        replaceAstNodeWith(
            node, nodeIs(symbol, VarDecl) ? symbol->varDecl.init : symbol);
        if (hasFlag(node, ComptimeIterable))
            node->next = symbol->next;
        node->flags &= ~flgComptime;
    }
}
