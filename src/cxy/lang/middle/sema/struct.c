//
// Created by Carter Mbotho on 2024-01-09.
//

#include "check.h"

#include "../builtins.h"
#include "../eval/eval.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"

#include "core/alloc.h"
#include "lang/middle/shake/shake.h"

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

                continue;
            }
        }
        prev = member;

        const Type *type = NULL;
        if (nodeIs(member, FuncDecl)) {
            type = member->type ?: checkFunctionSignature(visitor, member);
        }
        else {
            type = checkType(visitor, member);
            type = unwrapType(type, NULL);
            if (isClassType(type)) {
                node->flags |= flgReferenceMembers;
            }
            else if (isStructType(type) || isTupleType(type)) {
                node->flags |= (type->flags & flgReferenceMembers);
            }
        }

        if (typeIs(type, Error))
            node->type = ERROR_TYPE(ctx);
    }
}

static bool preCheckStructMembers(AstNode *node, NamedTypeMember *members)
{
    AstNode *member = node->structDecl.members;
    bool hasMemBuiltins = false;
    for (u64 i = 0; member; member = member->next, i++) {
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
                    // fallthrough
                case opDestructorOverload:
                    if (member->funcDecl.body != NULL)
                        node->flags |= flgImplementsDeinit;
                    // fallthrough
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
    return hasMemBuiltins;
}

static const Type *findCompatibleAnonymousType(AstVisitor *visitor,
                                               AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *fields = node->structExpr.fields, *field = fields;
    u64 count = countAstNodes(fields);
    NamedTypeMember *members = mallocOrDie(sizeof(NamedTypeMember) * count);
    NamedTypeMember **sortedMembers =
        mallocOrDie(sizeof(NamedTypeMember *) * count);
    for (u64 i = 0; field; field = field->next, i++) {
        AstNode *value = field->fieldExpr.value;
        value->parentScope = field;
        const Type *type = checkType(visitor, value);
        if (typeIs(type, Error)) {
            node->type = type;
            continue;
        }
        field->type = type;
        members[i] = (NamedTypeMember){
            .type = type, .name = field->fieldExpr.name, .decl = field};
        sortedMembers[i] = &members[i];
    }

    if (node->type == ERROR_TYPE(ctx)) {
        free(members);
        free(sortedMembers);
        return ERROR_TYPE(ctx);
    }

    qsort(sortedMembers,
          count,
          sizeof(sortedMembers[0]),
          sortCompareStructMember);

    const Type *type =
        findAnonymousStructType(ctx->types, sortedMembers, count, flgAnonymous);

    free(members);
    free(sortedMembers);

    if (type)
        return node->type = type;
    else
        return NULL;
}

static bool checkAnonymousStructExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *decl = NULL;
    const Type *type = findCompatibleAnonymousType(visitor, node);
    if (type == NULL) {
        AstNode *field = node->structExpr.fields;
        AstNodeList members = {};
        for (; field; field = field->next) {
            insertAstNode(
                &members,
                makeStructField(
                    ctx->pool,
                    &field->loc,
                    field->fieldExpr.name,
                    flgNone,
                    makeTypeReferenceNode(ctx->pool, field->type, &field->loc),
                    NULL,
                    NULL));
        }

        decl =
            makeStructDecl(ctx->pool,
                           &node->loc,
                           flgAnonymous,
                           makeAnonymousVariable(ctx->strings, "__AnonStruct"),
                           members.first,
                           NULL,
                           NULL);
        AstNode *builtins = createClassOrStructBuiltins(ctx->pool, decl),
                *builtin = builtins;
        for (; builtin; builtin = builtin->next) {
            builtin->funcDecl.this_ =
                makeFunctionParam(ctx->pool,
                                  &builtin->loc,
                                  S_this,
                                  NULL,
                                  NULL,
                                  node->flags & flgConst,
                                  builtin->funcDecl.signature->params);
            builtin->parentScope = decl;
        }
        insertAstNode(&members, builtins);
        decl->structDecl.members = members.first;
        decl->parentScope = ctx->root.parent;

        type = checkType(visitor, decl);
        if (typeIs(type, Error)) {
            node->type = type;
            return false;
        }

        astModifierAdd(&ctx->root, decl);
    }
    else if (typeIs(type, Error)) {
        return false;
    }
    else {
        decl = type->tStruct.decl;
    }

    node->structExpr.left = makeResolvedPath(ctx->pool,
                                             &node->loc,
                                             decl->structDecl.name,
                                             flgNone,
                                             decl,
                                             NULL,
                                             type);
    return true;
}

bool isExplicitConstructableFrom(Log *L, const Type *type, const Type *from)
{
    type = unwrapType(resolveAndUnThisType(type), NULL);
    if (!isClassOrStructType(type))
        return isTypeAssignableFrom(type, from);

    const Type *constructor = findStructMemberType(type, S_InitOverload);
    if (constructor == NULL)
        return false;

    constructor = matchOverloadedFunction(
        L, constructor, (const Type *[]){from}, 1, NULL, flgNone);

    if (constructor == NULL ||
        findAttribute(constructor->func.decl, S_explicit))
        return false;

    if (constructor->func.paramsCount != 1)
        return false;

    const Type *param = constructor->func.params[0];
    if (!isClassOrStructType(type))
        return isTypeAssignableFrom(param, from);

    if (!isExplicitConstructableFrom(L, param, from))
        return false;

    return true;
}

bool evalExplicitConstruction(AstVisitor *visitor,
                              const Type *type,
                              AstNode *node)
{
    const Type *source = unwrapType(
        resolveAndUnThisType(node->type ?: checkType(visitor, node)), NULL);
    type = resolveAndUnThisType(unwrapType(type, NULL));
    TypingContext *ctx = getAstVisitorContext(visitor);

    if (isTypeAssignableFrom(type, source))
        return true;

    if (!isClassOrStructType(type))
        return false;

    const NamedTypeMember *member = findStructMember(type, S_InitOverload);
    if (member == NULL)
        return false;

    const Type *constructor =
        matchOverloadedFunction(ctx->L,
                                member->type,
                                (const Type *[]){node->type},
                                1,
                                &node->loc,
                                flgNone);
    if (constructor == NULL ||
        findAttribute(constructor->func.decl, S_explicit))
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
    AstNode *left = node->structExpr.left;
    if (left == NULL && !checkAnonymousStructExpr(visitor, node))
        return;

    const Type *target = checkType(visitor, node->structExpr.left);
    if (typeIs(target, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }
    target = resolveType(target);
    if (!isStructType(target)) {
        logError(ctx->L,
                 &node->structExpr.left->loc,
                 "unsupported type used with struct initializer, '{t}' is not "
                 "a struct",
                 (FormatArg[]){{.t = target}});
        node->type = ERROR_TYPE(ctx);
        return;
    }
    node->structExpr.left->type = target;
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

        AstNode *value = field->fieldExpr.value;
        value->parentScope = field;
        field->type = member->type;
        field->fieldExpr.structField = member->decl;
        const Type *type = checkType(visitor, value);
        if (!isTypeAssignableFrom(member->type, type)) {
            logError(ctx->L,
                     &value->loc,
                     "value type '{t}' is not assignable to field type '{t}'",
                     (FormatArg[]){{.t = type}, {.t = member->type}});
            node->type = ERROR_TYPE(ctx);
            continue;
        }

        if (typeIs(member->type, Union) && member->type != type) {
            u32 idx = findUnionTypeIndex(member->type, type);
            csAssert0(idx != UINT32_MAX);
            field->fieldExpr.value = makeUnionValueExpr(ctx->pool,
                                                        &value->loc,
                                                        value->flags,
                                                        value,
                                                        idx,
                                                        NULL,
                                                        member->type);
            type = member->type;
        }
        else {
            value->type = member->type;
        }

        if (hasFlag(member->type, Optional) && !hasFlag(type, Optional)) {
            const Type *optionalTarget = getOptionalTargetType(member->type);
            if (nodeIs(value, NullLit)) {
                if (!transformOptionalNone(visitor, value, optionalTarget)) {
                    node->type = ERROR_TYPE(ctx);
                    continue;
                }
            }
            else {
                value->type = optionalTarget;
                if (!transformOptionalSome(
                        visitor, value, copyAstNode(ctx->pool, value))) //
                {
                    node->type = ERROR_TYPE(ctx);
                }
            }
        }

        field->type = member->type;
        initialized[member->decl->structField.index] = true;
    }

    if (node->type == ERROR_TYPE(ctx)) {
        free(initialized);
        return;
    }

    bool isExtern = hasFlag(striped, Extern);
    for (u64 i = 0; i < striped->tStruct.members->count; i++) {
        const AstNode *targetField = striped->tStruct.members->members[i].decl;
        if (initialized[i] || !nodeIs(targetField, FieldDecl))
            continue;

        if (targetField->structField.value == NULL) {
            if (!isExtern) {
                logError(ctx->L,
                         &node->loc,
                         "initializer expression missing struct required "
                         "member '{s}'",
                         (FormatArg[]){{.s = targetField->structField.name}});
                logNote(ctx->L,
                        &targetField->loc,
                        "struct field declared here",
                        NULL);
                node->type = ERROR_TYPE(ctx);
            }
            continue;
        }
        AstNode *temp = makeAstNode(
            ctx->pool,
            &(prev ?: node)->loc,
            &(AstNode){
                .tag = astFieldExpr,
                .type = targetField->type,
                .flags = targetField->flags,
                .fieldExpr = {.name = targetField->structField.name,
                              .value = deepCloneAstNode(
                                  ctx->pool, targetField->structField.value)}});
        if (prev)
            prev = prev->next = temp;
        else
            prev = node->structExpr.fields = temp;
    }

    if (node->type != ERROR_TYPE(ctx))
        node->type = target;
    free(initialized);
}

void checkStructDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    ctx->cls = getDeclarationName(node);
    node->structDecl.thisType =
        node->structDecl.thisType
            ?: makeThisType(ctx->types, node->structDecl.name, flgNone);
    const Type *this = node->structDecl.thisType;

    ctx->currentStruct = node;
    evaluateStructMembers(visitor, node);
    ctx->currentStruct = NULL;
    if (typeIs(node->type, Error))
        return;

    u64 membersCount = countAstNodes(node->structDecl.members);
    NamedTypeMember *members =
        mallocOrDie(sizeof(NamedTypeMember) * membersCount);

    ctx->currentStruct = node;
    bool hasMemBuiltins = preCheckStructMembers(node, members);
    ctx->currentStruct = NULL;

    if (typeIs(node->type, Error))
        goto checkStructMembersError;
    bool retype = false;
    ((Type *)this)->_this.that =
        makeStructType(ctx->types,
                       getDeclarationName(node),
                       members,
                       membersCount,
                       node,
                       node->flags & (flgTypeApplicable | flgReferenceMembers));
    node->type = this;

    if (hasMemBuiltins) {
        implementClassOrStructBuiltins(visitor, node);
        if (typeIs(node->type, Error))
            goto checkStructMembersError;
    }
    else {
        u64 removedCount = removeClassOrStructBuiltins(node, members);
        retype = removedCount > 0;
        membersCount -= removedCount;
    }

    ctx->currentStruct = node;
    if (checkMemberFunctions(visitor, node, members) || retype) {
        node->type = replaceStructType(
            ctx->types,
            this->_this.that,
            members,
            membersCount,
            node,
            node->flags & (flgReferenceMembers | flgTypeApplicable));
        ((Type *)this)->_this.that = node->type;
    }
    else
        node->type = this->_this.that;

    ctx->currentStruct = NULL;

checkStructMembersError:
    if (members)
        free(members);
}
