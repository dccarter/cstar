//
// Created by Carter on 2023-09-22.
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

static AstNode *implementCallStructCopyMember(TypingContext *ctx,
                                              AstNode *member,
                                              const FileLoc *loc)
{
    const Type *type = member->type;
    const NamedTypeMember *copy_ = findStructMember(type, S_CopyOverload);
    csAssert0(copy_);

    return makeCallExpr(
        ctx->pool,
        loc,
        makePathWithElements(ctx->pool,
                             loc,
                             flgNone,
                             makeResolvedPathElement(
                                 ctx->pool,
                                 loc,
                                 member->structField.name,
                                 member->flags | flgMember,
                                 member,
                                 makeResolvedPathElement(ctx->pool,
                                                         loc,
                                                         S_CopyOverload,
                                                         flgNone,
                                                         (AstNode *)copy_->decl,
                                                         NULL,
                                                         copy_->type),
                                 type),
                             NULL),
        NULL,
        flgNone,
        NULL,
        copy_->type->func.retType);
}

static AstNode *implementCallStructDeinitMember(TypingContext *ctx,
                                                AstNode *member,
                                                const FileLoc *loc)
{
    const Type *type = member->type;
    const NamedTypeMember *deinit_ =
        findStructMember(type, S_DestructorOverload);
    csAssert0(deinit_);

    return makeCallExpr(ctx->pool,
                        loc,
                        makePathWithElements(ctx->pool,
                                             loc,
                                             flgNone,
                                             makeResolvedPathElement(
                                                 ctx->pool,
                                                 loc,
                                                 member->structField.name,
                                                 member->flags | flgMember,
                                                 member,
                                                 makeResolvedPathElement(
                                                     ctx->pool,
                                                     loc,
                                                     S_DestructorOverload,
                                                     flgNone,
                                                     (AstNode *)deinit_->decl,
                                                     NULL,
                                                     deinit_->type),
                                                 type),
                                             NULL),
                        NULL,
                        flgNone,
                        NULL,
                        deinit_->type->func.retType);
}

static AstNode *implementCallClassDeinit(TypingContext *ctx,
                                         AstNode *node,
                                         const FileLoc *loc)
{
    const Type *type = node->type;
    const NamedTypeMember *deinit_ = findClassMember(type, S_DeinitOverload);
    if (deinit_ == NULL) {
        return NULL;
    }

    return makeCallExpr(ctx->pool,
                        loc,
                        makeResolvedPath(ctx->pool,
                                         loc,
                                         S_DeinitOverload,
                                         flgNone,
                                         (AstNode *)deinit_->decl,
                                         NULL,
                                         deinit_->type),
                        NULL,
                        flgNone,
                        NULL,
                        deinit_->type->func.retType);
}

static void implementStructCopyFunction(AstVisitor *visitor,
                                        AstNode *node,
                                        AstNode *copy)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    csAssert0(nodeIs(node, StructDecl));
    csAssert0(nodeIs(copy, FuncDecl) && copy->funcDecl.name == S_CopyOverload &&
              copy->funcDecl.body == NULL);

    AstNode *member = node->structDecl.members;
    AstNodeList stmts = {NULL};
    bool refMembers = false;
    for (; member; member = member->next) {
        if (!nodeIs(member, Field))
            continue;

        const Type *type = member->type;
        AstNode *expr = NULL;
        if (typeIs(type, Struct) && hasFlag(type, ReferenceMembers)) {
            const NamedTypeMember *copy_ =
                findStructMember(type, S_CopyOverload);
            csAssert0(copy_);
            expr = makeFieldExpr(
                ctx->pool,
                &copy->loc,
                member->structField.name,
                member->flags,
                implementCallStructCopyMember(ctx, member, &copy->loc),
                NULL);
            refMembers = true;
        }
        else if (typeIs(type, Class)) {
            expr = makeFieldExpr(ctx->pool,
                                 &copy->loc,
                                 member->structField.name,
                                 member->flags,
                                 makeGetReferenceCall(ctx, member, &copy->loc),
                                 NULL);
            refMembers = true;
        }
        else {
            expr = makeFieldExpr(ctx->pool,
                                 &copy->loc,
                                 member->structField.name,
                                 member->flags,
                                 makeResolvedPath(ctx->pool,
                                                  &copy->loc,
                                                  member->fieldExpr.name,
                                                  member->flags,
                                                  member,
                                                  NULL,
                                                  member->type),
                                 NULL);
        }

        type = checkType(visitor, expr);
        if (typeIs(type, Error)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }
        insertAstNode(&stmts, expr);
    }

    if (refMembers) {
        node->flags |= flgReferenceMembers;
        copy->funcDecl.body = makeExprStmt(
            ctx->pool,
            &copy->loc,
            flgNone,
            makeStructExprFromType(
                ctx->pool, &copy->loc, flgNone, stmts.first, NULL, node->type),
            NULL,
            node->type);
    }
    else {
        copy->funcDecl.body = makeBlockStmt(
            ctx->pool, &copy->loc, NULL, NULL, makeVoidType(ctx->types));
    }
}

static void implementDestructorFunction(AstVisitor *visitor,
                                        AstNode *node,
                                        AstNode *deinit)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    csAssert0(nodeIs(node, StructDecl) || nodeIs(node, ClassDecl));
    csAssert0(nodeIs(deinit, FuncDecl) &&
              deinit->funcDecl.name == S_DestructorOverload &&
              deinit->funcDecl.body == NULL);

    AstNode *member = node->structDecl.members;
    AstNodeList stmts = {NULL};
    if (nodeIs(node, ClassDecl)) {
        AstNode *callDeinit = implementCallClassDeinit(ctx, node, &deinit->loc);
        if (callDeinit) {
            node->flags |= flgImplementsDeinit;
            insertAstNode(&stmts, callDeinit);
        }
    }

    for (; member; member = member->next) {
        if (!nodeIs(member, Field))
            continue;

        const Type *type = member->type;
        AstNode *call = NULL;
        if (typeIs(type, Struct) && hasFlag(type, ReferenceMembers)) {
            const NamedTypeMember *deinit_ =
                findStructMember(type, S_DestructorOverload);
            csAssert0(deinit_);
            call = implementCallStructDeinitMember(ctx, member, &deinit->loc);
        }
        else if (typeIs(type, Class)) {
            call = makeDropReferenceCall(ctx, member, &deinit->loc);
        }
        else {
            continue;
        }

        type = checkType(visitor, call);
        if (typeIs(type, Error)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }
        insertAstNode(&stmts, call);
    }

    if (stmts.first) {
        node->flags |= flgReferenceMembers;
        deinit->funcDecl.body = makeBlockStmt(ctx->pool,
                                              &deinit->loc,
                                              stmts.first,
                                              NULL,
                                              makeVoidType(ctx->types));
    }
    else {
        deinit->funcDecl.body = makeBlockStmt(
            ctx->pool, &deinit->loc, NULL, NULL, makeVoidType(ctx->types));
    }
}

static void implementDestructorForwardFunction(AstVisitor *visitor,
                                               AstNode *node,
                                               AstNode *fwd)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    csAssert0(nodeIs(node, StructDecl) || nodeIs(node, ClassDecl));
    csAssert0(nodeIs(fwd, FuncDecl) && fwd->funcDecl.name == S_DestructorFwd &&
              fwd->funcDecl.body == NULL);

    if (!hasFlag(node, ReferenceMembers)) {
        fwd->funcDecl.body =
            makeBlockStmt(ctx->pool, &fwd->loc, NULL, NULL, NULL);
        return;
    }

    AstNode *destructorForward = findBuiltinDecl(S_destructorForward);
    csAssert0(destructorForward);

    AstNode *call = makeCallExpr(
        ctx->pool,
        &fwd->loc,
        makeResolvedPathWithArgs(ctx->pool,
                                 &fwd->loc,
                                 getDeclarationName(destructorForward),
                                 flgNone,
                                 destructorForward,
                                 makeResolvedPath(ctx->pool,
                                                  &fwd->loc,
                                                  node->structDecl.name,
                                                  flgNone,
                                                  node,
                                                  NULL,
                                                  node->type),
                                 NULL),
        makeResolvedPath(ctx->pool,
                         &fwd->loc,
                         S_ptr,
                         fwd->flags,
                         fwd->funcDecl.signature->params,
                         NULL,
                         NULL),
        flgNone,
        NULL,
        NULL);

    fwd->funcDecl.body =
        makeExprStmt(ctx->pool, &fwd->loc, flgNone, call, NULL, NULL);
}

static void implementHashFunction(AstVisitor *visitor,
                                  AstNode *node,
                                  AstNode *hash)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    csAssert0(nodeIs(node, StructDecl) || nodeIs(node, ClassDecl));
    csAssert0(nodeIs(hash, FuncDecl) && hash->funcDecl.name == S_HashOverload &&
              hash->funcDecl.body == NULL);

    AstNode *computeHash = findBuiltinDecl(
        nodeIs(node, StructDecl) ? S_computeStructHash : S_computeClassHash);
    csAssert0(computeHash);

    AstNode *call = makeCallExpr(
        ctx->pool,
        &hash->loc,
        makeResolvedPathWithArgs(ctx->pool,
                                 &hash->loc,
                                 getDeclarationName(computeHash),
                                 flgNone,
                                 computeHash,
                                 makeResolvedPath(ctx->pool,
                                                  &hash->loc,
                                                  node->structDecl.name,
                                                  flgNone,
                                                  node,
                                                  NULL,
                                                  node->type),
                                 NULL),
        makeResolvedPath(
            ctx->pool, &hash->loc, S_this, hash->flags, hash, NULL, NULL),
        flgNone,
        NULL,
        NULL);

    hash->funcDecl.body =
        makeExprStmt(ctx->pool, &hash->loc, flgNone, call, NULL, NULL);
}

static void implementStringFunction(AstVisitor *visitor,
                                    AstNode *node,
                                    AstNode *str)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    csAssert0(nodeIs(node, StructDecl) || nodeIs(node, ClassDecl));
    csAssert0(nodeIs(str, FuncDecl) && str->funcDecl.name == S_StringOverload &&
              str->funcDecl.body == NULL);

    AstNode *toString = findBuiltinDecl(
        nodeIs(node, StructDecl) ? S_structToString : S_classToString);
    csAssert0(toString);

    AstNode *call = makeCallExpr(
        ctx->pool,
        &str->loc,
        makeResolvedPathWithArgs(ctx->pool,
                                 &str->loc,
                                 getDeclarationName(toString),
                                 flgNone,
                                 toString,
                                 makeResolvedPath(ctx->pool,
                                                  &str->loc,
                                                  node->structDecl.name,
                                                  flgNone,
                                                  node,
                                                  NULL,
                                                  node->type),
                                 NULL),
        makeResolvedPath(
            ctx->pool,
            &str->loc,
            S_this,
            str->flags,
            str,
            makeResolvedPath(ctx->pool,
                             &str->loc,
                             str->funcDecl.signature->params->funcParam.name,
                             str->flags,
                             str->funcDecl.signature->params,
                             NULL,
                             NULL),
            NULL),
        flgNone,
        NULL,
        NULL);

    str->funcDecl.body =
        makeExprStmt(ctx->pool, &str->loc, flgNone, call, NULL, NULL);
}

AstNode *makeSliceConstructor(TypingContext *ctx,
                              const Type *slice,
                              AstNode *init)
{
    return makeCallExpr(
        ctx->pool,
        &init->loc,
        makeResolvedPath(ctx->pool,
                         &init->loc,
                         slice->name,
                         flgNone,
                         slice->tStruct.decl,
                         NULL,
                         slice),
        makeResolvedPath(
            ctx->pool,
            &init->loc,
            init->varDecl.name,
            init->flags,
            init,
            makeIntegerLiteral(ctx->pool,
                               &init->loc,
                               (i64)init->type->array.len,
                               NULL,
                               getPrimitiveType(ctx->types, prtU64)),
            makePointerType(
                ctx->types, init->type->array.elementType, flgNone)),
        flgNone,
        NULL,
        NULL);
}

AstNode *createClassOrStructBuiltins(MemPool *pool, AstNode *node)
{
    FileLoc loc = node->loc;
    loc.begin = loc.end;
    AstNodeList funcs = {NULL};

    if (nodeIs(node, StructDecl)) {
        insertAstNode(&funcs,
                      makeOperatorOverload(pool,
                                           &loc,
                                           opCopyOverload,
                                           NULL,
                                           NULL,
                                           NULL,
                                           flgNone,
                                           NULL,
                                           NULL));
    }

    if (isBuiltinsInitialized() ||
        findMemberByName(node, S_DestructorOverload) == NULL) {
        insertAstNode(&funcs,
                      makeOperatorOverload(pool,
                                           &loc,
                                           opDestructorOverload,
                                           NULL,
                                           NULL,
                                           NULL,
                                           flgNone,
                                           NULL,
                                           NULL));
    }

    if (findMemberByName(node, S_HashOverload) == NULL) {
        insertAstNode(&funcs,
                      makeOperatorOverload(pool,
                                           &loc,
                                           opHashOverload,
                                           NULL,
                                           NULL,
                                           NULL,
                                           flgConst,
                                           NULL,
                                           NULL));
    }

    if (findMemberByName(node, S_StringOverload) == NULL) {
        insertAstNode(
            &funcs,
            makeOperatorOverload(
                pool,
                &loc,
                opStringOverload,
                makeFunctionParam(
                    pool,
                    &loc,
                    S_sb,
                    makePath(pool, &loc, S_StringBuilder, flgNone, NULL),
                    NULL,
                    flgNone,
                    NULL),
                NULL,
                NULL,
                flgConst,
                NULL,
                NULL));
    }

    insertAstNode(
        &funcs,
        makeOperatorOverload(
            pool,
            &loc,
            opDestructorFwd,
            makeFunctionParam(pool,
                              &loc,
                              S_ptr,
                              makeVoidPointerAstNode(pool, &loc, flgNone, NULL),
                              NULL,
                              flgNone,
                              NULL),
            makeVoidAstNode(pool, &loc, flgNone, NULL, NULL),
            NULL,
            flgPure,
            NULL,
            NULL));

    return funcs.first;
}

void implementClassOrStructBuiltins(AstVisitor *visitor, AstNode *node)
{
    AstNode *member = node->structDecl.members;
    for (; member; member = member->next) {
        if (!nodeIs(member, FuncDecl) || member->funcDecl.body != NULL)
            continue;

        switch (member->funcDecl.operatorOverload) {
        case opDestructorOverload:
            implementDestructorFunction(visitor, node, member);
            break;
        case opCopyOverload:
            if (nodeIs(node, StructDecl))
                implementStructCopyFunction(visitor, node, member);
            break;
        case opHashOverload:
            implementHashFunction(visitor, node, member);
            break;
        case opStringOverload:
            implementStringFunction(visitor, node, member);
            break;
        case opDestructorFwd:
            implementDestructorForwardFunction(visitor, node, member);
            break;
        default:
            break;
        }
    }
}