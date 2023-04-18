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

void generateMemberExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *target = node->memberExpr.target,
                  *member = node->memberExpr.member;
    const Type *scope = isStructMethodRef(node);

    if (scope) {
        writeTypename(ctx, scope);
        format(ctx->state, "__{s}", (FormatArg[]){{.s = member->ident.value}});
    }
    else {
        astConstVisit(visitor, target);
        if (target->type->tag == typPointer)
            format(ctx->state, "->", NULL);
        else
            format(ctx->state, ".", NULL);
        if (member->tag == astIntegerLit) {
            format(ctx->state,
                   "_{u64}",
                   (FormatArg[]){{.u64 = member->intLiteral.value}});
        }
        else {
            format(
                ctx->state, "{s}", (FormatArg[]){{.s = member->ident.value}});
        }
    }
}

void checkMember(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *target = evalType(visitor, node->memberExpr.target);
    const Type *rawTarget = stripPointer(target);

    AstNode *member = node->memberExpr.member;
    node->flags |= (node->memberExpr.target->flags & flgConst);

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

        node->type = target->tuple.members[member->intLiteral.value];
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
    else if (stripPointer(target)->tag == typStruct) {
        if (member->tag != astIdentifier && member->tag != astPath) {
            logError(ctx->L,
                     &member->loc,
                     "unexpected member expression, expecting a struct member",
                     NULL);
            node->type = ERROR_TYPE(ctx);
            return;
        }
        AstNode *symbol = findSymbolByNode(ctx, rawTarget->tStruct.env, member);
        if (symbol == NULL)
            node->type = ERROR_TYPE(ctx);
        else
            node->type = symbol->type;
    }
    else {
        csAssert(member->tag == astIdentifier, "TODO");
        node->type = ERROR_TYPE(ctx);
    }
}
