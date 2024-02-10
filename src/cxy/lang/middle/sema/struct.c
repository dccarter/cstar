//
// Created by Carter Mbotho on 2024-01-09.
//

#include "check.h"

#include "../builtins.h"
#include "../eval/eval.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"

#include "core/alloc.h"

static void evaluateStructMembers(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *member = node->structDecl.members;
    AstNode *prev = NULL;
    for (u64 i = 0; member; member = member->next, i++) {
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

        const Type *type = NULL;
        if (nodeIs(member, FuncDecl)) {
            type = member->type ?: checkFunctionSignature(visitor, member);
            if (isBuiltinsInitialized() &&
                member->funcDecl.operatorOverload == opDeinitOverload) //
            {
                logError(ctx->L,
                         &member->loc,
                         "struct should not implement `deinit` operator, "
                         "structs are value types",
                         NULL);
                node->type = ERROR_TYPE(ctx);
                continue;
            }
        }
        else {
            type = checkType(visitor, member);
        }

        if (typeIs(type, Error))
            node->type = ERROR_TYPE(ctx);
    }
}

static void preCheckStructMembers(AstNode *node, NamedTypeMember *members)
{
    AstNode *member = node->structDecl.members;
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
}

bool isExplicitConstructableFrom(TypingContext *ctx,
                                 const Type *type,
                                 const Type *from)
{
    if (!typeIs(type, Struct))
        return isTypeAssignableFrom(type, from);

    const Type *constructor = findStructMemberType(type, S_InitOverload);
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

bool evalExplicitConstruction(AstVisitor *visitor,
                              const Type *type,
                              AstNode *node)
{
    const Type *source = node->type ?: checkType(visitor, node);
    TypingContext *ctx = getAstVisitorContext(visitor);

    if (isTypeAssignableFrom(type, source))
        return true;

    if (!typeIs(type, Struct))
        return false;

    const NamedTypeMember *member = findStructMember(type, S_InitOverload);
    if (member == NULL)
        return false;

    const Type *constructor =
        matchOverloadedFunction(ctx,
                                member->type,
                                (const Type *[]){node->type},
                                1,
                                &node->loc,
                                flgNone);
    if (constructor == NULL ||
        findAttribute(constructor->func.decl, "explicit"))
        return false;

    if (constructor->func.paramsCount != 1)
        return false;

    const Type *param = constructor->func.params[0];
    if (!evalExplicitConstruction(visitor, param, node))
        return false;

    AstNode *args = copyAstNode(ctx->pool, node);

    node->tag = astCallExpr;
    node->type = NULL;
    node->flags = flgNone;
    node->callExpr.callee =
        makePath(ctx->pool, &node->loc, type->name, flgNone, type);
    node->callExpr.args = args;

    type = transformToConstructCallExpr(visitor, node);
    return !typeIs(type, Error);
}

void checkStructExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *target = checkType(visitor, node->structExpr.left);
    if (typeIs(target, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (!isStructType(target)) {
        logError(ctx->L,
                 &node->structExpr.left->loc,
                 "unsupported type used with struct initializer, '{t}' is not "
                 "a struct",
                 (FormatArg[]){{.t = target}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    AstNode *field = node->structExpr.fields, *prev = node->structExpr.fields;
    const Type *striped = stripAll(target);
    bool *initialized =
        callocOrDie(1, sizeof(bool) * striped->tStruct.members->count);

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

        if (!nodeIs(member->decl, FieldDecl)) {
            logError(
                ctx->L,
                &field->loc,
                "member '{s}' is not a field, only struct can be initialized",
                (FormatArg[]){{.s = field->fieldExpr.name}});
            node->type = ERROR_TYPE(ctx);
            continue;
        }

        field->fieldExpr.value->parentScope = field;
        field->type = member->type;
        const Type *type = checkType(visitor, field->fieldExpr.value);
        if (!isTypeAssignableFrom(member->type, type)) {
            logError(ctx->L,
                     &field->fieldExpr.value->loc,
                     "value type '{t}' is not assignable to field type '{t}'",
                     (FormatArg[]){{.t = type}, {.t = member->type}});
            node->type = ERROR_TYPE(ctx);
            continue;
        }

        field->fieldExpr.value->type = member->type;
        field->type = member->type;
        initialized[member->decl->structField.index] = true;
    }

    if (node->type == ERROR_TYPE(ctx))
        return;

    for (u64 i = 0; i < striped->tStruct.members->count; i++) {
        const AstNode *targetField = striped->tStruct.members->members[i].decl;
        if (initialized[i] || !nodeIs(targetField, FieldDecl))
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

void checkStructDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *base = NULL;
    if (node->structDecl.base) {
        checkBaseDecl(visitor, node);
        if (typeIs(node->type, Error))
            return;
        base = node->structDecl.base->type;
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

    node->structDecl.thisType =
        node->structDecl.thisType
            ?: makeThisType(ctx->types, node->structDecl.name, flgNone);
    const Type *this = node->structDecl.thisType;

    ctx->currentStruct = node;
    evaluateStructMembers(visitor, node);
    ctx->currentStruct = NULL;
    if (typeIs(node->type, Error))
        goto checkStructInterfacesError;

    u64 membersCount = countAstNodes(node->structDecl.members);
    NamedTypeMember *members =
        mallocOrDie(sizeof(NamedTypeMember) * membersCount);

    ctx->currentStruct = node;
    preCheckStructMembers(node, members);
    ctx->currentStruct = NULL;

    if (typeIs(node->type, Error))
        goto checkStructMembersError;

    ((Type *)this)->_this.that =
        makeStructType(ctx->types,
                       getDeclarationName(node),
                       members,
                       membersCount,
                       node,
                       base,
                       implements,
                       implementsCount,
                       node->flags & flgTypeApplicable);
    node->type = this;

    implementClassOrStructBuiltins(visitor, node);
    if (typeIs(node->type, Error))
        goto checkStructMembersError;

    ctx->currentStruct = node;
    if (checkMemberFunctions(visitor, node, members)) {
        node->type = replaceStructType(
            ctx->types,
            this->_this.that,
            members,
            membersCount,
            node,
            base,
            implements,
            implementsCount,
            node->flags & (flgReferenceMembers | flgTypeApplicable));
        ((Type *)this)->_this.that = node->type;
    }
    else
        node->type = this->_this.that;

    ctx->currentStruct = NULL;

    if (!checkTypeImplementsAllMembers(ctx, node))
        node->type = ERROR_TYPE(ctx);

checkStructMembersError:
    if (members)
        free(members);

checkStructInterfacesError:
    if (implements)
        free(implements);
}
