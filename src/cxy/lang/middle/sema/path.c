//
// Created by Carter Mbotho on 2024-01-09.
//

#include "check.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"

static AstNode *resolveMember(TypingContext *ctx,
                              const Type *parent,
                              AstNode *node,
                              u64 *flags,
                              cstring name)
{
    AstNode *decl = findMemberDeclInType(parent, name);
    if (decl != NULL && decl->type != NULL) {
        *flags = decl->type->flags | decl->flags;
    }
    else {
        logError(ctx->L,
                 &node->loc,
                 "type '{t}' does not have a member named '{s}'",
                 (FormatArg[]){{.t = parent}, {.s = name}});
    }
    return decl;
}

static AstNode *resolveMemberUpInheritance(TypingContext *ctx,
                                           const Type *parent,
                                           AstNode *node,
                                           u64 *flags,
                                           cstring name,
                                           int depth)
{
    AstNode *decl = findMemberDeclInType(parent, name);
    if (decl == NULL) {
        const TypeInheritance *inheritance = getTypeInheritance(parent);
        const Type *base = inheritance ? inheritance->base : NULL;
        if (base) {
            return resolveMemberUpInheritance(
                ctx, base, node, flags, name, depth + 1);
        }
        else if (depth == 0) {
            logError(ctx->L,
                     &node->loc,
                     "type '{t}' does not have a member named '{s}'",
                     (FormatArg[]){{.t = parent}, {.s = name}});
        }
        return NULL;
    }
    if (nodeIs(node, Identifier))
        node->ident.super = depth;
    else
        node->pathElement.super = depth;

    *flags = decl->flags;
    return decl;
}

const Type *checkMember(AstVisitor *visitor, const Type *parent, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    cstring name =
        nodeIs(node, PathElem) ? node->pathElement.name : node->ident.value;
    u64 flags = flgNone;
    const Type *resolved = NULL;
    AstNode *decl = NULL;
    switch (parent->tag) {
    case typEnum:
        if (name == S___name) {
            resolved = makeStringType(ctx->types);
            flags |= flgEnumLiteral;
        }
        else {
            decl = resolveMember(ctx, parent, node, &flags, name);
            resolved = decl ? decl->type : NULL;
            flags = flgEnumLiteral;
        }
        break;
    case typThis:
        return checkMember(visitor, parent->_this.that, node);
    case typStruct:
    case typClass:
        decl = resolveMemberUpInheritance(ctx, parent, node, &flags, name, 0);
        resolved = decl ? decl->type : NULL;
        break;
    case typInterface:
    case typModule:
        decl = resolveMember(ctx, parent, node, &flags, name);
        resolved = decl ? decl->type : NULL;
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
    node->flags = flags;
    if (nodeIs(node, Identifier))
        node->ident.resolvesTo = decl;
    else
        node->pathElement.resolvesTo = decl;
    if (nodeIs(decl, Identifier) && hasFlag(decl, Define)) {
        node->_namedNode.name = decl->ident.value;
    }
    return resolved;
}

static const Type *checkBasePathElement(AstVisitor *visitor,
                                        AstNode *node,
                                        u64 flags)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    if (node->pathElement.isKeyword) {
        cstring keyword = node->pathElement.name;
        csAssert0(node->pathElement.enclosure);

        AstNode *enclosure = node->pathElement.enclosure;
        if (keyword == S_super) {
            node->type = enclosure->type;
            return getTypeBase(enclosure->type);
        }
        else if (keyword == S_this) {
            return node->type =
                       nodeIs(enclosure, StructDecl)
                           ? makePointerType(ctx->types,
                                             enclosure->type,
                                             enclosure->flags & flgConst)
                           : enclosure->type;
        }
        else if (keyword == S_This) {
            if (nodeIs(enclosure, StructDecl))
                return enclosure->structDecl.thisType;
            else
                return makePointerType(ctx->types,
                                       enclosure->structDecl.thisType,
                                       flags & flgConst);
        }
        unreachable("unsupported keyword");
    }
    else {
        csAssert0(node->pathElement.resolvesTo);
        AstNode *target = node->pathElement.resolvesTo;
        if (nodeIs(target, FieldDecl) || isMemberFunction(target))
            node->flags |= (flgMember | flgAddThis);
        return node->type =
                   target->type ?: checkTypeShallow(visitor, target, true);
    }
}

void checkPath(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *base = node->path.elements, *elem = base->next;
    if (hasFlag(node, Inherited)) {
        base->pathElement.resolvesTo = getBaseClassByName(
            ctx->currentStruct ?: ctx->currentClass, base->pathElement.name);
    }
    const Type *type = checkBasePathElement(visitor, base, node->flags);

    if (typeIs(type, Generic)) {
        type = resolveGenericDecl(visitor, base->pathElement.resolvesTo, base);
        if (type == NULL || typeIs(type, Error)) {
            node->type = type;
            return;
        }
    }

    u64 flags = base->flags | base->pathElement.resolvesTo->flags;
    const Type *prev = stripAll(type);
    for (; elem; elem = elem->next) {
        type = checkMember(visitor, prev, elem);
        if (typeIs(type, Error)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }
        flags = elem->flags;
        prev = type;
    }

    node->type = type;
    node->flags |= flags;
}
