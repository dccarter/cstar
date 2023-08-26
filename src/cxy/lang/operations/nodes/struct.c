//
// Created by Carter Mbotho on 2023-08-03.
//

#include "../check.h"

#include "lang/flag.h"
#include "lang/operations.h"
#include "lang/ttable.h"
#include "lang/types.h"
#include "lang/visitor.h"

#include "core/alloc.h"
#include "lang/ast.h"
#include "lang/strings.h"

static void checkImplements(AstVisitor *visitor,
                            AstNode *node,
                            const Type **implements,
                            u64 count)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *inf = node->structDecl.implements;
    for (u64 i = 0; inf; inf = inf->next, i++) {
        implements[i] = checkType(visitor, inf);
        if (typeIs(implements[i], Error)) {
            node->type = ERROR_TYPE(ctx);
            continue;
        }

        if (!typeIs(implements[i], Interface)) {
            logError(ctx->L,
                     &inf->loc,
                     "only interfaces can be implemented by structs, type "
                     "'{t}' is not an interface",
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
            logNote(
                ctx->L,
                &getNodeAtIndex(node->structDecl.implements, duplicate)->loc,
                "interface already implemented here",
                NULL);

            node->type = ERROR_TYPE(ctx);
        }
    }
}

static void preCheckMembers(AstVisitor *visitor,
                            AstNode *node,
                            StructMember *members)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *member = node->structDecl.members;

    for (u64 i = 0; member; member = member->next, i++) {
        const Type *type;
        if (nodeIs(member, FuncDecl)) {
            type = checkFunctionSignature(visitor, member);
            if (member->funcDecl.operatorOverload == opDelete)
                node->flags |= flgImplementsDelete;
        }
        else {
            type = checkType(visitor, member);
        }

        if (typeIs(type, Error)) {
            node->type = ERROR_TYPE(ctx);
            continue;
        }

        if (nodeIs(member, StructField)) {
            members[i] = (StructMember){
                .name = member->structField.name, .type = type, .decl = member};
            member->structField.index = i;
        }
        else {
            members[i] = (StructMember){.name = getDeclarationName(member),
                                        .type = type,
                                        .decl = member};
        }
    }

    if (typeIs(node->type, Error))
        return;
}

static bool checkMemberFunctions(AstVisitor *visitor,
                                 AstNode *node,
                                 StructMember *members)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *member = node->structDecl.members;

    bool retype = false;
    for (u64 i = 0; member; member = member->next, i++) {
        if (nodeIs(member, FuncDecl)) {
            const Type *type = checkFunctionBody(visitor, member);
            if (typeIs(type, Error)) {
                node->type = ERROR_TYPE(ctx);
                return false;
            }
            retype = members[i].type != type;
            members[i].type = type;
        }
    }

    return retype;
}

bool isExplicitConstructableFrom(TypingContext *ctx,
                                 const Type *type,
                                 const Type *from)
{
    if (!typeIs(type, Struct))
        return isTypeAssignableFrom(type, from);

    const Type *constructor = findStructMemberType(type, S_New);
    if (constructor == NULL)
        return false;

    constructor = matchOverloadedFunction(
        ctx, constructor, (const Type *[]){from}, 1, NULL, flgNone);

    if (constructor == NULL ||
        findAttribute(constructor->func.decl, S_explicit))
        return false;

    if (constructor->func.paramsCount != 1)
        return false;

    const Type *param = constructor->func.params[0];
    if (!typeIs(param, Struct))
        return isTypeAssignableFrom(param, from);

    if (!isExplicitConstructableFrom(ctx, param, from))
        return false;

    return true;
}

void checkStructExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *target = checkType(visitor, node->structExpr.left);
    if (typeIs(target, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (!typeIs(target, Struct)) {
        logError(ctx->L,
                 &node->structExpr.left->loc,
                 "unsupported type used with struct initializer, '{t}' is not "
                 "a struct",
                 (FormatArg[]){{.t = target}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (target->tStruct.base) {
        logError(ctx->L,
                 &node->structExpr.left->loc,
                 "type '{t}' cannot be initialized with an initializer "
                 "expression, struct extends a base type",
                 (FormatArg[]){{.t = target}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    AstNode *field = node->structExpr.fields, *prev = node->structExpr.fields;
    bool *initialized =
        callocOrDie(1, sizeof(bool) * target->tStruct.membersCount);

    for (; field; field = field->next) {
        prev = field;
        const StructMember *member =
            findStructMember(target, field->fieldExpr.name);
        if (member == NULL) {
            logError(
                ctx->L,
                &field->loc,
                "field '{s}' does not exist in target struct type '{t}'",
                ((FormatArg[]){{.s = field->fieldExpr.name}, {.t = target}}));
            node->type = ERROR_TYPE(ctx);
            continue;
        }

        if (!nodeIs(member->decl, StructField)) {
            logError(
                ctx->L,
                &field->loc,
                "member '{s}' is not a field, only struct can be initialized",
                (FormatArg[]){{.s = field->fieldExpr.name}});
            node->type = ERROR_TYPE(ctx);
            continue;
        }

        const Type *type = checkType(visitor, field->fieldExpr.value);
        if (!isTypeAssignableFrom(member->type, type)) {
            logError(ctx->L,
                     &field->fieldExpr.value->loc,
                     "value type '{t}' is not assignable to field type '{t}'",
                     (FormatArg[]){{.t = type}, {.t = member->type}});
            node->type = ERROR_TYPE(ctx);
            continue;
        }

        field->type = member->type;
        initialized[member->decl->structField.index] = true;
    }

    if (node->type == ERROR_TYPE(ctx))
        return;

    for (u64 i = 0; i < target->tStruct.membersCount; i++) {
        const AstNode *targetField = target->tStruct.members[i].decl;
        if (initialized[i] || !nodeIs(targetField, StructField))
            continue;

        if (targetField->structField.value == NULL) {
            logError(
                ctx->L,
                &node->loc,
                "initializer expression missing struct required member '{s}'",
                (FormatArg[]){{.s = targetField->structField.name}});
            logNote(
                ctx->L, &targetField->loc, "struct field declared here", NULL);
            node->type = ERROR_TYPE(ctx);
            continue;
        }
        AstNode *temp = makeAstNode(
            ctx->pool,
            &(prev ?: node)->loc,
            &(AstNode){.tag = astFieldExpr,
                       .type = targetField->type,
                       .flags = targetField->flags,
                       .fieldExpr = {.name = targetField->structField.name,
                                     .value = targetField->structField.value}});
        if (prev)
            prev = prev->next = temp;
        else
            prev = node->structExpr.fields = temp;
    }

    if (node->type != ERROR_TYPE(ctx))
        node->type = target;
}

void checkStructField(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *type =
        checkType(visitor, node->structField.type) ?: makeAutoType(ctx->types);
    if (typeIs(type, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    const Type *value = checkType(visitor, node->structField.value);
    if (typeIs(value, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (value && !isTypeAssignableFrom(type, value)) {
        logError(ctx->L,
                 &node->structField.value->loc,
                 "field initializer of type '{t}' not compatible with "
                 "field type '{t}'",
                 (FormatArg[]){{.t = value}, {.t = type}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    node->type = typeIs(type, Auto) ? value : type;
}

void checkStructDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *base = checkType(visitor, node->structDecl.base);

    if (base) {
        if (typeIs(base, Error)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }
        if (!typeIs(base, Struct)) {
            logError(
                ctx->L,
                &node->structDecl.base->loc,
                "base of type of '{t}' is not supported, base must be a struct",
                (FormatArg[]){{.t = base}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
    }
    const Type **implements = NULL;
    u64 implementsCount = countAstNodes(node->structDecl.implements);
    if (implementsCount) {
        u64 count = countAstNodes(node->structDecl.implements);
        implements = mallocOrDie(sizeof(Type *) * count);
        checkImplements(visitor, node, implements, count);
    }

    if (typeIs(node->type, Error))
        goto checkStructInterfacesError;

    u64 membersCount = countAstNodes(node->structDecl.members);
    StructMember *members = mallocOrDie(sizeof(StructMember) * membersCount);
    node->structDecl.thisType =
        node->structDecl.thisType
            ?: makeThisType(ctx->types, node->structDecl.name, flgNone);
    const Type *this = node->structDecl.thisType;

    ctx->currentStruct = node;
    preCheckMembers(visitor, node, members);
    ctx->currentStruct = NULL;

    if (typeIs(node->type, Error))
        goto checkStructMembersError;

    node->type =
        makeStruct(ctx->types,
                   &(Type){.tag = typStruct,
                           .flags = node->flags,
                           .name = node->structDecl.name,
                           .tStruct = {.base = base,
                                       .members = members,
                                       .membersCount = membersCount,
                                       .interfaces = implements,
                                       .interfacesCount = implementsCount,
                                       .decl = node}});

    ((Type *)this)->this.that = node->type;

    ctx->currentStruct = node;
    if (checkMemberFunctions(visitor, node, members)) {
        node->type = replaceStructType(
            ctx->types,
            node->type,
            &(Type){.tag = typStruct,
                    .flags = node->flags,
                    .name = node->structDecl.name,
                    .tStruct = {.base = base,
                                .members = members,
                                .membersCount = membersCount,
                                .interfaces = implements,
                                .interfacesCount = implementsCount,
                                .decl = node}});
        ((Type *)this)->this.that = node->type;
    }
    ctx->currentStruct = NULL;

    for (u64 i = 0; i < implementsCount; i++) {
        const Type *interface = implements[i];
        for (u64 j = 0; j < interface->tInterface.membersCount; j++) {
            const StructMember *member = &interface->tInterface.members[j];
            const StructMember *found =
                findStructMember(node->type, member->name);
            if (found == NULL || !typeIs(found->type, Func)) {
                logError(ctx->L,
                         &getNodeAtIndex(node->structDecl.implements, i)->loc,
                         "struct missing interface method '{s}' implementation",
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
                         &getNodeAtIndex(node->structDecl.implements, i)->loc,
                         "struct missing interface method '{s}' implementation",
                         (FormatArg[]){{.s = member->name}});
                logNote(ctx->L,
                        &member->decl->loc,
                        "interface method declared here",
                        NULL);
                node->type = ERROR_TYPE(ctx);
            }
        }
    }

checkStructMembersError:
    if (members)
        free(members);

checkStructInterfacesError:
    if (implements)
        free(implements);
}