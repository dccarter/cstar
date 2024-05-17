//
// Created by Carter Mbotho on 2024-01-09.
//

#include "check.h"

#include "../eval/eval.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"

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
    case astFuncParamDecl:
        type = parent->funcParam.type->type;
        break;
    case astFieldExpr:
        type = parent->type;
        break;
    case astFieldDecl:
        type = parent->structField.type ? parent->structField.type->type : NULL;
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
            node->memberExpr.target =
                makeCastExpr(ctx->pool,
                             &target->loc,
                             target->flags,
                             target,
                             makeTypeReferenceNode(
                                 ctx->pool,
                                 member->ident.resolvesTo->parentScope->type,
                                 &target->loc),
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

        if (member->intLiteral.isNegative ||
            integerLiteralValue(member) >= target_->tuple.count) {
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
