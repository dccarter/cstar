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

static bool preCheckClassMembers(AstNode *node, NamedTypeMember *members)
{
    AstNode *member = node->classDecl.members;
    bool hasMemBuiltins = false;
    for (u64 i = hasFlag(node, Virtual); member; member = member->next, i++) {
        if (nodeIs(member, FieldDecl)) {
            members[i] = (NamedTypeMember){.name = member->structField.name,
                                           .type = member->type,
                                           .decl = member};
            member->structField.index = i;
            hasMemBuiltins = hasMemBuiltins || isClassType(member->type) ||
                             isDestructible(member->type);
        }
        else {
            members[i] = (NamedTypeMember){.name = getDeclarationName(member),
                                           .type = member->type,
                                           .decl = member};
            if (nodeIs(member, FuncDecl)) {
                switch (member->funcDecl.operatorOverload) {
                case opDeinitOverload:
                    node->flags |= flgImplementsDeinit;
                    // fallthrough
                case opDestructorOverload:
                case opDestructorFwd:
                    hasMemBuiltins =
                        hasMemBuiltins || member->funcDecl.body != NULL;
                    break;
                default:
                    break;
                }
            }
        }
    }

    if (hasMemBuiltins)
        return true;
    while (node->classDecl.base) {
        node = node->classDecl.base;
        if (isDestructible(node->type))
            return true;
        node = resolveAstNode(node);
    }
    return false;
}

static void patchDefaultInitializer(TypingContext *ctx,
                                    AstNode *init,
                                    AstNode *vTableMember,
                                    AstNode *value)
{
    csAssert0(value);
    AstNode *stmt =
        makeExprStmt(ctx->pool,
                     builtinLoc(),
                     flgNone,
                     makeAssignExpr(ctx->pool,
                                    builtinLoc(),
                                    flgNone,
                                    makeResolvedPath(ctx->pool,
                                                     builtinLoc(),
                                                     vTableMember->_name,
                                                     flgAddThis,
                                                     vTableMember,
                                                     NULL,
                                                     vTableMember->type),
                                    opAssign,
                                    value,
                                    NULL,
                                    vTableMember->type),
                     NULL,
                     vTableMember->type);
    if (init->funcDecl.body->blockStmt.stmts)
        getLastAstNode(init->funcDecl.body->blockStmt.stmts)->next = stmt;
    else
        init->funcDecl.body->blockStmt.stmts = stmt;
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
    csAssert0(isClassType(type));
    return makeExprStmt(
        ctx->pool,
        loc,
        flgNone,
        makeBackendCallExpr(
            ctx->pool,
            loc,
            flgNone,
            bfiDrop,
            makeResolvedPath(ctx->pool,
                             loc,
                             member->structField.name,
                             member->flags | flgMember | flgAddThis,
                             member,
                             NULL,
                             type),
            makeVoidType(ctx->types)),
        NULL,
        makeVoidType(ctx->types));
}

AstNode *makeCopyReferenceCall(TypingContext *ctx,
                               AstNode *member,
                               const FileLoc *loc)
{
    const Type *type = member->type;
    csAssert0(typeIs(type, Class));
    return makeBackendCallExpr(
        ctx->pool,
        loc,
        flgNone,
        bfiCopy,
        makeResolvedPath(ctx->pool,
                         loc,
                         member->structField.name,
                         member->flags | flgMember | flgAddThis,
                         member,
                         NULL,
                         type),
        makeVoidPointerType(ctx->types, flgNone));
}

void checkClassDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type **implements = NULL, *baseType = NULL;
    AstNode *base = node->classDecl.base, *vTableMember = NULL;
    ctx->cls = getDeclarationName(node);
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

    u64 membersCount = countAstNodes(node->classDecl.members) + isVirtual + 1;
    NamedTypeMember *members =
        mallocOrDie(sizeof(NamedTypeMember) * membersCount);

    AstNode *defaultInit =
        implementDefaultInitializer(visitor, node, isVirtual || vTableMember);
    if (!defaultInit) {
        if (typeIs(node->type, Error))
            goto checkClassInterfacesError;
        membersCount--;
    }
    ctx->currentClass = node;
    bool hasMemBuiltins = preCheckClassMembers(node, members);
    ctx->currentClass = NULL;

    if (typeIs(node->type, Error))
        goto checkClassMembersError;

    if (!hasMemBuiltins) {
        membersCount = removeClassOrStructBuiltins(node, members);
    }

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

    if (hasMemBuiltins) {
        implementClassOrStructBuiltins(visitor, node);
        if (typeIs(node->type, Error))
            goto checkClassMembersError;
    }

    ctx->currentClass = node;
    bool retype = checkMemberFunctions(visitor, node, members);
    if (!retype && typeIs(node->type, Error))
        goto checkClassMembersError;

    if (isVirtual) {
        vTableMember = inheritanceBuildVTableType(visitor, node);
        members[0].name = S_vtable;
        members[0].decl = vTableMember;
        if (members[0].decl == NULL)
            goto checkClassMembersError;
        members[0].type = members[0].decl->type;
        csAssert0(defaultInit);
        patchDefaultInitializer(
            ctx,
            defaultInit,
            vTableMember,
            deepCloneAstNode(ctx->pool, vTableMember->structField.value));
        retype = true;
    }
    else if (vTableMember != NULL) {
        csAssert0(defaultInit);
        patchDefaultInitializer(
            ctx, defaultInit, vTableMember, inheritanceBuildVTable(ctx, node));
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
