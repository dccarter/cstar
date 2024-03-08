//
// Created by Carter on 2023-09-21.
//
#include "check.h"

#include "lang/frontend/flag.h"

#include "core/alloc.h"

void checkImplements(AstVisitor *visitor,
                     AstNode *node,
                     const Type **implements,
                     u64 count)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *inf = node->classDecl.implements;
    for (u64 i = 0; inf; inf = inf->next, i++) {
        implements[i] = checkType(visitor, inf);
        if (typeIs(implements[i], Error)) {
            node->type = ERROR_TYPE(ctx);
            continue;
        }

        if (!typeIs(implements[i], Interface)) {
            logError(ctx->L,
                     &inf->loc,
                     "only interfaces can be implemented by structs or "
                     "classes, type '{t}' is not an interface",
                     (FormatArg[]){{.t = implements[i]}});
            node->type = ERROR_TYPE(ctx);
            continue;
        }

        int duplicate = findTypeInArray(implements, i, implements[i]);
        if (duplicate >= 0) {
            logError(ctx->L,
                     &inf->loc,
                     "duplicate interface type '{t}'",
                     (FormatArg[]){{.t = implements[i]}});
            logNote(ctx->L,
                    &getNodeAtIndex(node->classDecl.implements, duplicate)->loc,
                    "interface already implemented here",
                    NULL);

            node->type = ERROR_TYPE(ctx);
        }
    }
}

bool checkTypeImplementsAllMembers(TypingContext *ctx, AstNode *node)
{
    const TypeInheritance *inheritance = getTypeInheritance(node->type);
    csAssert0(inheritance);

    for (u64 i = 0; i < inheritance->interfacesCount; i++) {
        const Type *interface = inheritance->interfaces[i];
        for (u64 j = 0; j < interface->tInterface.members->count; j++) {
            const NamedTypeMember *member =
                &interface->tInterface.members->members[j];
            const NamedTypeMember *found =
                findStructMember(node->type, member->name);
            if (found == NULL || !typeIs(found->type, Func)) {
                logError(ctx->L,
                         &getNodeAtIndex(node->classDecl.implements, i)->loc,
                         "struct missing interface method "
                         "'{s}' implementation",
                         (FormatArg[]){{.s = member->name}});
                logNote(ctx->L,
                        &member->decl->loc,
                        "interface method declared here",
                        NULL);
                node->type = ERROR_TYPE(ctx);
                continue;
            }

            const Type *match =
                matchOverloadedFunction(ctx,
                                        found->type,
                                        member->type->func.params,
                                        member->type->func.paramsCount,
                                        NULL,
                                        member->type->flags);
            if (match == NULL ||
                match->func.retType != member->type->func.retType) {
                logError(ctx->L,
                         &getNodeAtIndex(node->classDecl.implements, i)->loc,
                         "struct missing interface method "
                         "'{s}' implementation",
                         (FormatArg[]){{.s = member->name}});
                logNote(ctx->L,
                        &member->decl->loc,
                        "interface method declared here",
                        NULL);
                node->type = ERROR_TYPE(ctx);
            }
        }
    }

    return !typeIs(node->type, Error);
}

void checkInterfaceDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *member = node->interfaceDecl.members;
    u64 membersCount = countAstNodes(member);
    NamedTypeMember *members =
        mallocOrDie(sizeof(NamedTypeMember) * membersCount);

    for (u64 i = 0; member; member = member->next, i++) {
        const Type *type = checkType(visitor, member);
        if (typeIs(type, Error)) {
            node->type = ERROR_TYPE(ctx);
            continue;
        }
        members[i] = (NamedTypeMember){
            .name = getDeclarationName(member), .type = type, .decl = member};
    }

    if (typeIs(node->type, Error))
        goto checkInterfaceMembersError;

    node->type = makeInterfaceType(ctx->types,
                                   getDeclarationName(node),
                                   members,
                                   membersCount,
                                   node,
                                   node->flags & flgTypeApplicable);

checkInterfaceMembersError:
    if (members)
        free(members);
}
