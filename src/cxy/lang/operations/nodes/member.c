//
// Created by Carter on 2023-08-31.
//

#include "../check.h"
#include "../codegen.h"
#include "../eval.h"

#include "lang/capture.h"
#include "lang/flag.h"
#include "lang/operations.h"
#include "lang/strings.h"
#include "lang/ttable.h"
#include "lang/visitor.h"

#include "core/alloc.h"

static inline const Type *isStructMethodRef(const AstNode *node)
{
    return typeIs(node->type, Func) && node->type->func.decl &&
                   node->type->func.decl->parentScope &&
                   isClassOrStructAstNode(node->type->func.decl->parentScope)
               ? node->type->func.decl->parentScope->type
               : NULL;
}

static bool evalIntegerMemberExpr(EvalContext *ctx, AstNode *node)
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

    *node = *getNodeAtIndex(target->tupleExpr.elements, i);
    return true;
}

static bool evalStringMemberExpr(EvalContext *ctx, AstNode *node)
{
    AstNode *target = node->memberExpr.target;
    AstNode *member = node->memberExpr.member;
    target = nodeIs(target, GroupExpr) ? target->groupExpr.expr : target;

    if (nodeIs(target, EnumDecl)) {
        AstNode *value =
            findEnumOptionByName(target, member->stringLiteral.value);
        if (value == NULL)
            node->tag = astNullLit;
        else
            replaceAstNode(node, value->enumOption.value);
        return true;
    }

    logError(ctx->L,
             &target->loc,
             "comp-time member expression operator only supported on enum "
             "types",
             NULL);
    node->tag = astError;
    return false;
}

static const Type *determineMemberTargetType(TypingContext *ctx,
                                             const AstNode *parent,
                                             AstNode *node)
{
    if (parent == NULL)
        return NULL;

    const Type *type = NULL;
    switch (parent->tag) {
    case astVarDecl:
        type = parent->varDecl.type ? parent->varDecl.type->type : NULL;
        break;
    case astAssignExpr:
    case astBinaryExpr:
        type = parent->assignExpr.lhs->type;
        break;
    case astSwitchStmt:
        type = parent->switchStmt.cond->type;
        break;
    case astCaseStmt:
        return determineMemberTargetType(ctx, parent->parentScope, node);
    case astFuncParam:
        type = parent->funcParam.type->type;
        break;
    case astFieldExpr:
        type = parent->type;
        break;
    default:
        break;
    }

    if (type == NULL) {
        logError(ctx->L,
                 &node->loc,
                 "shorthand member expression not supported on current context",
                 NULL);
        return NULL;
    }

    const Type *stripped = stripOnce(type, NULL);
    if (!typeIs(stripped, Enum)) {
        logError(ctx->L,
                 &node->loc,
                 "shorthand member expression on type '{t}', only supported on "
                 "enum types",
                 (FormatArg[]){{.t = type}});
    }

    node->memberExpr.target = makeResolvedPath(ctx->pool,
                                               &node->loc,
                                               type->name,
                                               type->flags & flgConst,
                                               type->tEnum.decl,
                                               NULL,
                                               type);
    return type;
}

void generateMemberExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *target = node->memberExpr.target,
                  *member = node->memberExpr.member;
    const Type *target_ = stripOnce(target->type, NULL);
    const Type *scope = isStructMethodRef(node);

    if (scope) {
        if (nodeIs(member, Identifier)) {
            writeTypename(ctx, scope);
            format(ctx->state, "__", NULL);
        }
        astConstVisit(visitor, member);
    }
    else if (typeIs(target_, Enum)) {
        writeTypename(ctx, target_);
        format(ctx->state, "_", NULL);
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

void checkMemberExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *target = node->memberExpr.target,
            *member = node->memberExpr.member;
    const Type *target_ =
        target ? checkType(visitor, target)
               : determineMemberTargetType(ctx, node->parentScope, node);
    if (target_ == NULL) {
        node->type = ERROR_TYPE(ctx);
        return;
    }
    if (typeIs(target_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (hasFlag(member, Comptime) && !evaluate(ctx->evaluator, member)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    target_ = stripAll(target_);
    if (nodeIs(member, Identifier)) {
        node->type = checkMember(visitor, target_, member);
        if (node->type == NULL) {
            logError(ctx->L,
                     &member->loc,
                     "type '{t}' does not have member named '{s}'",
                     (FormatArg[]){{.t = target_}, {.s = member->ident.value}});
            node->type = ERROR_TYPE(ctx);
        }
        if (member->ident.super) {
            node->memberExpr.target = makeMemberExpr(
                ctx->pool,
                &target->loc,
                target->flags,
                target,
                makeIdentifier(ctx->pool,
                               &target->loc,
                               S_super,
                               member->ident.super - 1,
                               NULL,
                               member->ident.resolvesTo->parentScope->type),
                NULL,
                member->ident.resolvesTo->parentScope->type);
            member->ident.super--;
        }
    }
    else if (nodeIs(member, Path)) {
        AstNode *name = member->path.elements;
        node->type = checkMember(visitor, target_, name);
        if (node->type == NULL) {
            logError(
                ctx->L,
                &member->loc,
                "type '{t}' does not have member named '{s}'",
                (FormatArg[]){{.t = target_}, {.s = name->pathElement.name}});
            node->type = ERROR_TYPE(ctx);
        }
    }
    else if (nodeIs(member, IntegerLit)) {
        if (!typeIs(target_, Tuple)) {
            logError(ctx->L,
                     &member->loc,
                     "type '{t}' does not support integer literal member "
                     "access, expecting expression of tuple type",
                     (FormatArg[]){{.t = target_}});
            node->type = ERROR_TYPE(ctx);
            return;
        }

        if (member->intLiteral.hasMinus ||
            member->intLiteral.value >= target_->tuple.count) {
            logError(ctx->L,
                     &member->loc,
                     "tuple {t} member access out of range, expecting "
                     "'0-{u64}', got '{i64}",
                     (FormatArg[]){{.t = target_},
                                   {.u64 = target_->tuple.count - 1},
                                   {.i64 = member->intLiteral.value}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
        node->type = target_->tuple.members[member->intLiteral.value];
        node->flags =
            (target_->flags & flgConst) | (node->type->flags & flgConst);
    }
}

void evalMemberExpr(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    AstNode *target = node->memberExpr.target;

    if (!evaluate(visitor, target)) {
        node->tag = astError;
        return;
    }

    AstNode *member = node->memberExpr.member;
    if (nodeIs(member, IntegerLit)) {
        evalIntegerMemberExpr(ctx, node);
    }
    else if (nodeIs(member, StringLit)) {
        evalStringMemberExpr(ctx, node);
    }
    else if (nodeIs(member, Identifier)) {
        AstNode *value = evalAstNodeMemberAccess(
            ctx,
            &node->loc,
            nodeIs(target, Ref) ? target->reference.target : target,
            member->ident.value);
        if (value == NULL) {
            node->tag = astError;
            return;
        }

        replaceAstNode(node, value);
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
