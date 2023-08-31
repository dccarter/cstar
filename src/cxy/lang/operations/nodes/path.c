//
// Created by Carter on 2023-08-27.
//
#include "../check.h"
#include "../codegen.h"

#include "lang/capture.h"
#include "lang/flag.h"
#include "lang/operations.h"
#include "lang/strings.h"
#include "lang/ttable.h"
#include "lang/visitor.h"

#include "core/alloc.h"

const Type *checkPathElement(AstVisitor *visitor,
                             const Type *parent,
                             AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *resolved = NULL;
    switch (parent->tag) {
    case typThis:
    case typStruct:
    case typInterface:
    case typContainer:
    case typModule:
    case typEnum:
        resolved = expectSymbolInType(
            ctx->types, parent, ctx->L, node->pathElement.name, &node->loc);
        break;
    default:
        logError(ctx->L,
                 &node->loc,
                 "expression of type '{t}' does not support member expressions",
                 (FormatArg[]){{.t = parent}});
        break;
    }

    if (resolved == NULL || typeIs(resolved, Error))
        return node->type = ERROR_TYPE(ctx);

    if (typeIs(resolved, Generic)) {
        resolved = resolveGenericDecl(visitor, resolved->generic.decl, node);
        if (typeIs(resolved, Error))
            return resolved;
    }

    node->type = resolved;
    node->flags = resolved->flags;
    if (typeIs(parent, Enum))
        node->flags |= flgEnumLiteral;

    return resolved;
}

static const Type *checkBasePathElement(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    if (node->pathElement.isKeyword) {
        cstring keyword = node->pathElement.name;
        csAssert0(node->pathElement.enclosure);
        AstNode *parent = getParentScope(node->pathElement.enclosure);
        if (keyword == S_super) {
            return node->type = parent->type->tStruct.base;
        }
        else if (keyword == S_this) {
            return node->type = parent->type;
        }
        else if (keyword == S_This) {
            return node->pathElement.enclosure->structDecl.thisType;
        }
        unreachable("unsupported keyword");
    }
    else {
        csAssert0(node->pathElement.resolvesTo);
        node->flags |=
            (nodeIs(node->pathElement.resolvesTo, StructField) ? flgMember
                                                               : flgNone);
        return node->type = node->pathElement.resolvesTo->type;
    }
}

void generatePathElement(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (hasFlag(node, Member)) {
        format(ctx->state,
               "this->{s}{s}",
               (FormatArg[]){{.s = hasFlag(node, Inherited) ? "super." : ""},
                             {.s = node->pathElement.name}});
    }
    else
        format(ctx->state, "{s}", (FormatArg[]){{.s = node->pathElement.name}});
}

void generatePath(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *parent =
        typeIs(node->type, Func) ? node->type->func.decl->parentScope : NULL;

    if (typeIs(node->type, Enum) &&
        hasFlag(node->path.elements->next, EnumLiteral)) {
        writeEnumPrefix(ctx, node->type);
        generateManyAstsWithDelim(
            visitor, "_", "_", "", node->path.elements->next);
    }
    else if (hasFlag(node, BuiltinMember) || nodeIs(parent, StructDecl)) {
        const Type *scope = parent->type;
        const AstNode *func = node->type->func.decl;
        writeTypename(ctx, scope);
        format(ctx->state, "__{s}", (FormatArg[]){{.s = func->funcDecl.name}});
    }
    else {
        const AstNode *elem = node->path.elements;
        if (hasFlag(elem, AppendNS)) {
            if (ctx->namespace == NULL && nodeIs(elem->parentScope, Program)) {
                AstNode *module = elem->parentScope->program.module;
                if (module) {
                    writeDeclNamespace(ctx, module->moduleDecl.name, NULL);
                }
            }
            else {
                writeNamespace(ctx, NULL);
            }
        }

        for (; elem; elem = elem->next) {
            astConstVisit(visitor, elem);
            if (elem->next) {
                cstring name = elem->pathElement.name;
                if (typeIs(elem->type, Module))
                    format(ctx->state, "__", NULL);
                else if (elem->type &&
                         ((name == S_this) || typeIs(elem->type, Pointer) ||
                          typeIs(elem->type, This) || isSliceType(elem->type)))
                    format(ctx->state, "->", NULL);
                else
                    format(ctx->state, ".", NULL);
            }
        }
    }
}

void checkPath(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *base = node->path.elements, *elem = base->next;
    if (hasFlag(node, Inherited)) {
        base->pathElement.resolvesTo =
            getBaseClassByName(ctx->currentStruct, base->pathElement.name);
    }
    const Type *type = checkBasePathElement(visitor, base);

    if (typeIs(type, Generic)) {
        type = resolveGenericDecl(visitor, base->pathElement.resolvesTo, base);
        if (type == NULL || typeIs(type, Error)) {
            node->type = type;
            return;
        }
    }

    u64 flags = base->flags | base->pathElement.resolvesTo->flags;
    for (; elem; elem = elem->next) {
        type = checkPathElement(visitor, stripAll(type), elem);
        if (typeIs(type, Error)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }
        flags = elem->flags;
    }

    node->type = type;
    node->flags = flags;
}
