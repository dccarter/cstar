/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-18
 */

#include "lang/codegen.h"
#include "lang/eval.h"
#include "lang/semantics.h"

#include "lang/ttable.h"

static inline const Type *isStructMethodRef(const AstNode *node)
{
    return (node->type->tag == typFunc && node->type->func.decl &&
            node->type->func.decl->parentScope &&
            node->type->func.decl->parentScope->tag == astStructDecl)
               ? node->type->func.decl->parentScope->type
               : NULL;
}

static bool evalIntegerMemberExpr(SemanticsContext *ctx, AstNode *node)
{
    AstNode *target = node->memberExpr.target;
    AstNode *member = node->memberExpr.member;

    if (!nodeIs(target, TupleExpr) && !nodeIs(target, TupleType)) {
        logError(ctx->L,
                 &target->loc,
                 "comp-time member expression operator only supported on tuple "
                 "expressions or type declarations",
                 NULL);
        node->tag = astError;
        return false;
    }

    i64 i = (i64)getNumericLiteral(member);
    u64 len = target->tupleExpr.len;
    if (i < 0 || i >= len) {
        logError(ctx->L,
                 &node->loc,
                 "member out of bounds for comp-time integer member "
                 "expression, requested index '{i64}', expecting '< {u64'}",
                 (FormatArg[]){{.i64 = i}, {.u64 = len}});

        node->tag = astError;
        return false;
    }

    *node = *getNodeAtIndex(target->tupleExpr.args, i);
    return true;
}

static bool evalStringMemberExpr(SemanticsContext *ctx, AstNode *node)
{
    AstNode *target = node->memberExpr.target;
    AstNode *member = node->memberExpr.member;

    if (!nodeIs(target, EnumDecl)) {
        logError(ctx->L,
                 &target->loc,
                 "comp-time member expression operator only supported on enum "
                 "types",
                 NULL);
        node->tag = astError;
        return false;
    }

    AstNode *value = findEnumOptionByName(target, member->stringLiteral.value);
    if (value == NULL)
        node->tag = astNullLit;
    else
        *node = *value->enumOption.value;
    return true;
}

void generateMemberExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *target = node->memberExpr.target,
                  *member = node->memberExpr.member;
    const Type *scope = isStructMethodRef(node);

    if (scope) {
        if (nodeIs(member, Identifier)) {
            writeTypename(ctx, scope);
            format(ctx->state, "__", NULL);
        }
        astConstVisit(visitor, member);
    }
    else {
        astConstVisit(visitor, target);
        if (typeIs(target->type, Pointer) || isSliceType(target->type))
            format(ctx->state, "->", NULL);
        else
            format(ctx->state, ".", NULL);
        if (nodeIs(member, IntegerLit)) {
            format(ctx->state,
                   "_{u64}",
                   (FormatArg[]){{.u64 = member->intLiteral.value}});
        }
        else {
            astConstVisit(visitor, member);
        }
    }
}

void checkMember(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *target = node->memberExpr.target->type
                             ?: evalType(visitor, node->memberExpr.target);
    const Type *rawTarget = stripAll(target);

    AstNode *member = node->memberExpr.member;
    node->flags |= (node->memberExpr.target->flags & flgConst);
    if (hasFlag(member, Comptime) && !evaluate(visitor, member)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (member->tag == astIntegerLit) {
        u64 flags = target->flags;
        if (rawTarget->tag != typTuple) {
            logError(ctx->L,
                     &node->memberExpr.target->loc,
                     "literal member expression cannot be used on type '{t}', "
                     "type is not a tuple",
                     (FormatArg[]){{.t = target}});
            node->type = ERROR_TYPE(ctx);
            return;
        }

        if (member->intLiteral.value >= target->tuple.count) {
            logError(ctx->L,
                     &member->loc,
                     "literal member '{u64}' out of range, tuple '{t}' has "
                     "{u64} members",
                     (FormatArg[]){{.u64 = member->intLiteral.value},
                                   {.t = target},
                                   {.u64 = target->tuple.count}});
            node->type = ERROR_TYPE(ctx);
            return;
        }

        node->type = rawTarget->tuple.members[member->intLiteral.value];
        node->flags |= ((flags | node->type->flags) & flgConst);
    }
    else if (rawTarget->tag == typEnum) {
        if (member->tag != astIdentifier && member->tag != astPath) {
            logError(ctx->L,
                     &member->loc,
                     "unexpected member expression, expecting an enum member",
                     NULL);
            node->type = ERROR_TYPE(ctx);
            return;
        }
        AstNode *option = findSymbolByNode(ctx, rawTarget->tEnum.env, member);
        if (option == NULL)
            node->type = ERROR_TYPE(ctx);
        else
            node->type = target;
    }
    else if (typeIs(rawTarget, Struct)) {
        if (nodeIs(member, Identifier)) {
            AstNode *element = makeAstNode(
                ctx->pool,
                &member->loc,
                &(AstNode){.tag = astPathElem,
                           .flags = member->flags,
                           .type = member->type,
                           .pathElement.name = member->ident.value});
            clearAstBody(member);
            member->tag = astPath;
            member->path.elements = element;
        }

        if (!nodeIs(member, Path)) {
            logError(ctx->L,
                     &member->loc,
                     "unexpected member expression, expecting a struct member",
                     NULL);
            node->type = ERROR_TYPE(ctx);
            return;
        }

        AstNode *symbol = findSymbolByNode(ctx, rawTarget->tStruct.env, member);
        if (symbol != NULL) {
            if (nodeIs(symbol, GenericDecl)) {
                symbol = checkGenericDeclReference(visitor,
                                                   symbol,
                                                   member->path.elements,
                                                   rawTarget->tStruct.env);
            }

            if (symbol != NULL) {
                member->type = symbol->type;
                member->path.elements->type = symbol->type;
                member->path.elements->pathElement.resolvesTo = symbol;
            }
        }

        if (symbol == NULL) {
            node->type = ERROR_TYPE(ctx);
            return;
        }

        node->type = symbol->type;
    }
    else {
        csAssert(nodeIs(member, Identifier), "TODO");
        node->type = ERROR_TYPE(ctx);
    }
}

void evalMemberExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *target = node->memberExpr.target;

    if (!evaluate(visitor, target)) {
        node->tag = astError;
        return;
    }

    AstNode *member = node->memberExpr.member;
    if (!evaluate(visitor, member)) {
        node->tag = astError;
        return;
    };

    if (nodeIs(member, IntegerLit)) {
        evalIntegerMemberExpr(ctx, node);
    }
    else if (nodeIs(member, Identifier)) {
        evalStringMemberExpr(ctx, node);
    }
    else {
        logError(
            ctx->L,
            &node->loc,
            "unexpected comp-time member expression, target can either be a "
            "tuple expression or an enum type",
            NULL);
        node->tag = astError;
    }
}
