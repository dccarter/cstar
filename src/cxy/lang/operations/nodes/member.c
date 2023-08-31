//
// Created by Carter on 2023-08-31.
//

#include "../check.h"
#include "../codegen.h"

#include "lang/capture.h"
#include "lang/flag.h"
#include "lang/operations.h"
#include "lang/strings.h"
#include "lang/ttable.h"
#include "lang/visitor.h"

#include "core/alloc.h"

static inline const Type *isStructMethodRef(const AstNode *node)
{
    return (node->type->tag == typFunc && node->type->func.decl &&
            node->type->func.decl->parentScope &&
            node->type->func.decl->parentScope->tag == astStructDecl)
               ? node->type->func.decl->parentScope->type
               : NULL;
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

void checkMemberExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *target = node->memberExpr.target,
            *member = node->memberExpr.member;
    const Type *target_ = checkType(visitor, target);
    if (typeIs(target_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (nodeIs(member, Path)) {
        node->type =
            checkPathElement(visitor, stripAll(target_), member->path.elements);
    }
    else if (nodeIs(member, IntegerLit)) {
        target_ = stripAll(target_);
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
