//
// Created by Carter on 2023-09-21.
//

#include "../check.h"
#include "../codegen.h"

#include "lang/ast.h"
#include "lang/builtins.h"
#include "lang/flag.h"
#include "lang/operations.h"
#include "lang/strings.h"
#include "lang/ttable.h"
#include "lang/types.h"
#include "lang/visitor.h"

#include "core/alloc.h"

#include <string.h>

static void checkClassBaseDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *base = checkType(visitor, node->classDecl.base);
    csAssert0(base);
    if (typeIs(base, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (!typeIs(base, Class)) {
        logError(ctx->L,
                 &node->classDecl.base->loc,
                 "base of type of '{t}' is not supported, base must be a class",
                 (FormatArg[]){{.t = base}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    const AstNode *finalized = findAttribute(base->tClass.decl, S_final);
    if (finalized != NULL) {
        logError(
            ctx->L,
            &node->classDecl.base->loc,
            "base class {t} cannot be extended, base class marked as final",
            (FormatArg[]){{.t = base}});
        node->type = ERROR_TYPE(ctx);
        return;
    }
}

static void preCheckMembers(AstVisitor *visitor,
                            AstNode *node,
                            NamedTypeMember *members)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *member = node->classDecl.members;

    for (u64 i = 0; member; member = member->next, i++) {
        const Type *type;
        if (nodeIs(member, FuncDecl)) {
            type = checkFunctionSignature(visitor, member);
            if (member->funcDecl.operatorOverload == opDeinitialize)
                node->flags |= flgImplementsDelete;
        }
        else {
            type = checkType(visitor, member);
        }

        if (typeIs(type, Error)) {
            node->type = ERROR_TYPE(ctx);
            continue;
        }

        if (nodeIs(member, Field)) {
            members[i] = (NamedTypeMember){
                .name = member->structField.name, .type = type, .decl = member};
            member->structField.index = i;
        }
        else {
            members[i] = (NamedTypeMember){.name = getDeclarationName(member),
                                           .type = type,
                                           .decl = member};
        }
    }

    if (typeIs(node->type, Error))
        return;
}

void checkClassExpr(AstVisitor *visitor, AstNode *node)
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

    AstNode *field = node->structExpr.fields, *prev = node->structExpr.fields;
    bool *initialized =
        callocOrDie(1, sizeof(bool) * target->tStruct.members->count);

    for (; field; field = field->next) {
        prev = field;
        const NamedTypeMember *member =
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

        if (!nodeIs(member->decl, Field)) {
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

    for (u64 i = 0; i < target->tStruct.members->count; i++) {
        const AstNode *targetField = target->tStruct.members->members[i].decl;
        if (initialized[i] || !nodeIs(targetField, Field))
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

void checkClassDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type **implements = NULL, *base = NULL;

    if (node->classDecl.base) {
        checkClassBaseDecl(visitor, node);
        if (typeIs(node->type, Error))
            return;
        base = node->classDecl.base->type;
    }

    u64 implementsCount = countAstNodes(node->classDecl.implements);
    if (implementsCount) {
        u64 count = countAstNodes(node->classDecl.implements);
        implements = mallocOrDie(sizeof(Type *) * count);
        checkImplements(visitor, node, implements, count);

        if (typeIs(node->type, Error))
            goto checkClassInterfacesError;
    }

    u64 membersCount = countAstNodes(node->classDecl.members);
    NamedTypeMember *members =
        mallocOrDie(sizeof(NamedTypeMember) * membersCount);
    node->classDecl.thisType =
        node->classDecl.thisType
            ?: makeThisType(ctx->types, node->classDecl.name, flgNone);
    const Type *this = node->classDecl.thisType;

    ctx->currentClass = node;
    preCheckMembers(visitor, node, members);
    ctx->currentClass = NULL;

    if (typeIs(node->type, Error))
        goto checkClassMembersError;

    ((Type *)this)->this.that = makeClassType(ctx->types,
                                              getDeclarationName(node),
                                              members,
                                              membersCount,
                                              node,
                                              base,
                                              implements,
                                              implementsCount,
                                              node->flags & flgTypeApplicable);
    node->type = this;

    ctx->currentClass = node;
    if (checkMemberFunctions(visitor, node, members)) {
        node->type = replaceClassType(ctx->types,
                                      this->this.that,
                                      members,
                                      membersCount,
                                      node,
                                      base,
                                      implements,
                                      implementsCount,
                                      node->flags & flgTypeApplicable);
        ((Type *)this)->this.that = node->type;
    }
    else
        node->type = this->this.that;

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
