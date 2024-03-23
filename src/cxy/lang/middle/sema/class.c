//
// Created by Carter on 2023-09-21.
//

#include "check.h"

#include "../builtins.h"
#include "../eval/eval.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"

#include "core/alloc.h"

void checkBaseDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *base = checkType(visitor, node->classDecl.base);
    csAssert0(base);
    if (typeIs(base, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (nodeIs(node, ClassDecl) && !typeIs(base, Class)) {
        logError(ctx->L,
                 &node->classDecl.base->loc,
                 "base of type of '{t}' is not supported, base must be a class",
                 (FormatArg[]){{.t = base}});
        node->type = ERROR_TYPE(ctx);
        return;
    }
    else if (nodeIs(node, StructDecl) && !typeIs(base, Struct)) {
        logError(
            ctx->L,
            &node->classDecl.base->loc,
            "base of type of '{t}' is not supported, base must be a struct",
            (FormatArg[]){{.t = base}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    const AstNode *finalized = findAttribute(base->tClass.decl, S_final);
    if (finalized != NULL) {
        logError(ctx->L,
                 &node->classDecl.base->loc,
                 "base {t} cannot be extended, base class marked as final",
                 (FormatArg[]){{.t = base}});
        node->type = ERROR_TYPE(ctx);
        return;
    }
}

static void evalClassMembers(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *member = node->classDecl.members;
    AstNode *prev = NULL;
    for (u64 i = 0; member; member = member->next, i++) {
        const Type *type;
        if (hasFlag(member, Comptime)) {
            if (!evaluate(ctx->evaluator, member)) {
                node->type = ERROR_TYPE(ctx);
                return;
            }
            if (nodeIs(member, Noop)) {
                if (prev == NULL) {
                    node->structDecl.members = member->next;
                }
                else {
                    prev->next = member->next;
                }
                member = member->next;
                member->parentScope = node;
            }
        }
        prev = member;
        member->parentScope = node;

        if (nodeIs(member, FuncDecl)) {
            type = checkFunctionSignature(visitor, member);
            node->flags |= member->flags & (flgAbstract | flgVirtual);
        }
        else {
            type = checkType(visitor, member);
        }

        if (typeIs(type, Error))
            node->type = ERROR_TYPE(ctx);
    }
}

static void preCheckClassMembers(AstNode *node, NamedTypeMember *members)
{
    AstNode *member = node->classDecl.members;

    for (u64 i = 0; member; member = member->next, i++) {
        if (nodeIs(member, FieldDecl)) {
            members[i] = (NamedTypeMember){.name = member->structField.name,
                                           .type = member->type,
                                           .decl = member};
            member->structField.index = i;
        }
        else {
            members[i] = (NamedTypeMember){.name = getDeclarationName(member),
                                           .type = member->type,
                                           .decl = member};
        }
    }

    if (typeIs(node->type, Error))
        return;
}

AstNode *makeAllocateCall(TypingContext *ctx, AstNode *node)
{
    const Type *type = node->type;
    csAssert0(isClassType(type));
    AstNode *new = findBuiltinDecl(S_allocate);
    csAssert0(new);

    return makeCallExpr(
        ctx->pool,
        &node->loc,
        makeResolvedPathWithArgs(ctx->pool,
                                 &node->loc,
                                 getDeclarationName(new),
                                 flgNone,
                                 new,
                                 makeResolvedPath(ctx->pool,
                                                  &node->loc,
                                                  type->name,
                                                  flgNone,
                                                  type->tClass.decl,
                                                  NULL,
                                                  type),
                                 NULL),
        NULL,
        flgNone,
        NULL,
        NULL);
}

AstNode *makeDropReferenceCall(TypingContext *ctx,
                               AstNode *member,
                               const FileLoc *loc)
{
    const Type *type = member->type;
    csAssert0(typeIs(type, Class));
    AstNode *drop = findBuiltinDecl(S_dropref);
    csAssert0(drop);
    return makeCallExpr(
        ctx->pool,
        loc,
        makePathWithElements(
            ctx->pool,
            loc,
            flgNone,
            makeResolvedPathElement(
                ctx->pool, loc, S_dropref, drop->flags, drop, NULL, NULL),
            NULL),
        makeResolvedPath(ctx->pool,
                         loc,
                         member->structField.name,
                         member->flags | flgMember,
                         member,
                         NULL,
                         member->type),
        flgNone,
        NULL,
        NULL);
}

AstNode *makeGetReferenceCall(TypingContext *ctx,
                              AstNode *member,
                              const FileLoc *loc)
{
    const Type *type = member->type;
    csAssert0(typeIs(type, Class));
    AstNode *get = findBuiltinDecl(S_getref);
    csAssert0(get);
    return makeCallExpr(
        ctx->pool,
        loc,
        makePathWithElements(
            ctx->pool,
            loc,
            flgNone,
            makeResolvedPathElement(
                ctx->pool, loc, S_getref, get->flags, get, NULL, NULL),
            NULL),
        makeResolvedPath(ctx->pool,
                         loc,
                         member->structField.name,
                         member->flags | flgMember,
                         member,
                         NULL,
                         member->type),
        flgNone,
        NULL,
        NULL);
}

void checkClassDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type **implements = NULL, *baseType = NULL;

    if (node->classDecl.base) {
        checkBaseDecl(visitor, node);
        if (typeIs(node->type, Error))
            return;
        baseType = node->classDecl.base->type;
    }

    u64 implementsCount = countAstNodes(node->classDecl.implements);
    if (implementsCount) {
        u64 count = countAstNodes(node->classDecl.implements);
        implements = mallocOrDie(sizeof(Type *) * count);
        checkImplements(visitor, node, implements, count);

        if (typeIs(node->type, Error))
            goto checkClassInterfacesError;
    }

    node->classDecl.thisType =
        node->classDecl.thisType
            ?: makeThisType(ctx->types, node->classDecl.name, flgNone);
    const Type *this = node->classDecl.thisType;

    node->type = this;
    ctx->currentClass = node;
    evalClassMembers(visitor, node);
    ctx->currentClass = NULL;
    if (typeIs(node->type, Error))
        goto checkClassInterfacesError;

    u64 membersCount = countAstNodes(node->classDecl.members);
    NamedTypeMember *members =
        mallocOrDie(sizeof(NamedTypeMember) * membersCount);

    ctx->currentClass = node;
    preCheckClassMembers(node, members);
    ctx->currentClass = NULL;

    if (typeIs(node->type, Error))
        goto checkClassMembersError;

    ((Type *)this)->_this.that = makeClassType(ctx->types,
                                               getDeclarationName(node),
                                               members,
                                               membersCount,
                                               node,
                                               baseType,
                                               implements,
                                               implementsCount,
                                               node->flags & flgTypeApplicable);
    node->type = this;

    implementClassOrStructBuiltins(visitor, node);
    if (typeIs(node->type, Error))
        goto checkClassMembersError;

    ctx->currentClass = node;
    bool retype = checkMemberFunctions(visitor, node, members);
    if (!retype && typeIs(node->type, Error))
        goto checkClassMembersError;

    if (retype) {
        node->type = replaceClassType(ctx->types,
                                      this->_this.that,
                                      members,
                                      membersCount,
                                      node,
                                      baseType,
                                      implements,
                                      implementsCount,
                                      node->flags & flgTypeApplicable);
        ((Type *)this)->_this.that = node->type;
    }
    else {
        node->type = this->_this.that;
    }
    ctx->currentClass = NULL;

    if (!checkTypeImplementsAllMembers(ctx, node))
        node->type = ERROR_TYPE(ctx);

checkClassMembersError:
    if (members)
        free(members);

checkClassInterfacesError:
    if (implements)
        free(implements);
}
