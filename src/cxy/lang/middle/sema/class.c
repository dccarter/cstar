//
// Created by Carter on 2023-09-21.
//

#include "check.h"

#include "../builtins.h"
#include "../eval/eval.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"

#include "core/alloc.h"

static inline bool isInheritable(AstNode *member)
{
    return nodeIs(member, FieldDecl) &&
           !(hasFlag(member, Static) || hasFlag(member, VTable));
}

static void inheritClassMembers(TypingContext *ctx,
                                AstNodeList *into,
                                AstNode *base)
{
    if (base->classDecl.base) {
        inheritClassMembers(ctx, into, resolvePath(base->classDecl.base));
    }

    AstNode *member = base->classDecl.members;
    for (; member; member = member->next) {
        if (isInheritable(member)) {
            AstNode *inherited = deepCloneAstNode(ctx->pool, member);
            inherited->flags |= flgInherited;
            insertAstNode(into, inherited);
        }
    }
}

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

    const AstNode *finalized = findAttribute(base->tClass.decl, S_final);
    if (finalized != NULL) {
        logError(ctx->L,
                 &node->classDecl.base->loc,
                 "base {t} cannot be extended, base class marked as final",
                 (FormatArg[]){{.t = base}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    AstNodeList inheritedMembers = {NULL};
    inheritClassMembers(
        ctx, &inheritedMembers, resolvePath(node->classDecl.base));
    if (inheritedMembers.first != NULL) {
        inheritedMembers.last->next = node->classDecl.members;
        node->classDecl.members = inheritedMembers.first;
    }
}

static void evalClassMembers(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *member = node->classDecl.members;
    AstNode *prev = NULL;
    for (u64 i = 0; member; i++) {
        const Type *type;
        if (hasFlag(member, Comptime)) {
            if (!evaluate(ctx->evaluator, member)) {
                node->type = ERROR_TYPE(ctx);
                return;
            }

            if (nodeIs(member, Noop)) {
                while (nodeIs(member, Noop))
                    member = member->next;
                if (prev == NULL) {
                    node->structDecl.members = member;
                }
                else {
                    prev->next = member;
                }
                member->parentScope = node;
                continue;
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

        member = member->next;
    }
}

static void preCheckClassMembers(AstNode *node, NamedTypeMember *members)
{
    AstNode *member = node->classDecl.members;
    AstNodeList constructors = {};
    for (u64 i = hasFlag(node, Virtual); member; member = member->next, i++) {
        if (nodeIs(member, FieldDecl)) {
            members[i] = (NamedTypeMember){.name = member->structField.name,
                                           .type = member->type,
                                           .decl = member};
            member->structField.index = i;
            node->flags |=
                (member->structField.value ? flgDefaultedFields : flgNone);
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
    AstNode *drop =
        findBuiltinDecl(ctx->traceMemory ? S_sptr_drop_trace : S_sptr_drop);
    csAssert0(drop);
    return makeExprStmt(
        ctx->pool,
        loc,
        flgNone,
        makeCallExpr(ctx->pool,
                     loc,
                     makeResolvedPath(ctx->pool,
                                      loc,
                                      drop->_namedNode.name,
                                      drop->flags,
                                      drop,
                                      NULL,
                                      drop->type),
                     makeResolvedPath(ctx->pool,
                                      loc,
                                      member->structField.name,
                                      member->flags | flgMember | flgAddThis,
                                      member,
                                      ctx->traceMemory
                                          ? makeSrLocNode(ctx->pool, loc)
                                          : NULL,
                                      makeVoidPointerType(ctx->types, flgNone)),
                     flgNone,
                     NULL,
                     drop->type->func.retType),
        NULL,
        makeVoidType(ctx->types));
}

AstNode *makeGetReferenceCall(TypingContext *ctx,
                              AstNode *member,
                              const FileLoc *loc)
{
    const Type *type = member->type;
    csAssert0(typeIs(type, Class));
    AstNode *get =
        findBuiltinDecl(ctx->traceMemory ? S_sptr_get_trace : S_sptr_ref);
    csAssert0(get);
    return makeCallExpr(
        ctx->pool,
        loc,
        makeResolvedPathElement(ctx->pool,
                                loc,
                                get->_namedNode.name,
                                get->flags,
                                get,
                                NULL,
                                get->type),
        makeResolvedPath(ctx->pool,
                         loc,
                         member->structField.name,
                         member->flags | flgMember | flgAddThis,
                         member,
                         ctx->traceMemory ? makeSrLocNode(ctx->pool, loc)
                                          : NULL,
                         makeVoidPointerType(ctx->types, flgNone)),
        flgNone,
        NULL,
        get->type->func.retType);
}

void checkClassDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type **implements = NULL, *baseType = NULL;
    AstNode *base = node->classDecl.base, *vTableMember = NULL;
    if (node->type)
        return;

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

    u8 isVirtual = hasFlag(node, Virtual);
    if (hasFlag(base, Virtual) && !isVirtual) {
        // Add - vtable: &Base_VTable member
        vTableMember = inheritanceMakeVTableMember(ctx, node, NULL);
    }

    u64 membersCount = countAstNodes(node->classDecl.members) + isVirtual;
    NamedTypeMember *members =
        mallocOrDie(sizeof(NamedTypeMember) * membersCount);

    ctx->currentClass = node;
    preCheckClassMembers(node, members);
    ctx->currentClass = NULL;

    if (typeIs(node->type, Error))
        goto checkClassMembersError;

    ((Type *)this)->_this.that = makeClassType(ctx->types,
                                               getDeclarationName(node),
                                               &members[isVirtual],
                                               membersCount - isVirtual,
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

    if (isVirtual) {
        members[0].name = S_vtable;
        members[0].decl = inheritanceBuildVTableType(visitor, node);
        if (members[0].decl == NULL)
            goto checkClassMembersError;
        members[0].type = members[0].decl->type;
        retype = true;
    }
    else if (vTableMember != NULL) {
        vTableMember->structField.value = inheritanceBuildVTable(ctx, node);
    }

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
