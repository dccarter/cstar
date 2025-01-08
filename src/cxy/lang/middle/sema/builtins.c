//
// Created by Carter Mbotho on 2024-01-09.
//

#include "check.h"

#include "../builtins.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"

static inline bool hasMember(AstNode *node, cstring name)
{
    return findInAstNode(node, name) != NULL;
}

static AstNode *implementCallTupleCopy(TypingContext *ctx,
                                       AstNode *member,
                                       const FileLoc *loc)
{
    const Type *type = member->type;
    const Type *copy_ = type->tuple.copyFunc;
    csAssert0(copy_);

    return makeBackendCallExpr(
        ctx->pool,
        loc,
        flgNone,
        bfiCopy,
        makeResolvedPath(ctx->pool,
                         &member->loc,
                         member->structField.name,
                         member->flags | flgMember | flgAddThis,
                         member,
                         NULL,
                         type),
        copy_->func.retType);
}

static AstNode *implementCallStructCopyMember(TypingContext *ctx,
                                              AstNode *member,
                                              const FileLoc *loc)
{
    const Type *type = member->type;
    const NamedTypeMember *copy_ = findStructMember(type, S_CopyOverload);
    csAssert0(copy_);

    return makeBackendCallExpr(
        ctx->pool,
        loc,
        flgNone,
        bfiCopy,
        makeResolvedPath(ctx->pool,
                         loc,
                         member->structField.name,
                         member->flags | flgMember | flgAddThis,
                         member,
                         NULL,
                         type),
        copy_->type->func.retType);
}

static AstNode *implementCallStructDestructor(TypingContext *ctx,
                                              AstNode *member,
                                              const FileLoc *loc)
{
    const Type *type = member->type;
    const NamedTypeMember *destructor =
        findStructMember(type, S_DestructorOverload);
    csAssert0(destructor);

    return makeExprStmt(
        ctx->pool,
        loc,
        flgNone,
        makeBackendCallExpr(
            ctx->pool,
            loc,
            flgNone,
            bfiDrop,
            makeResolvedPath(ctx->pool,
                             loc,
                             member->structField.name,
                             member->flags | flgMember | flgAddThis,
                             member,
                             NULL,
                             type),
            destructor->type->func.retType),
        NULL,
        makeVoidType(ctx->types));
}

static AstNode *implementCallClassDestructor(TypingContext *ctx,
                                             AstNode *node,
                                             const Type *base,
                                             const FileLoc *loc)
{
    AstNode *dctor = findMemberDeclInType(base, S_DestructorOverload);
    AstNode *this = getMemberFunctionThis(node);
    csAssert0(this);
    if (dctor == NULL) {
        dctor = findMemberDeclInType(base, S_DeinitOverload);
        if (dctor == NULL)
            return NULL;
    }
    const Type *type = makeReferenceType(ctx->types, base, flgNone);
    return makeExprStmt(
        ctx->pool,
        loc,
        flgNone,
        makeCallExpr(
            ctx->pool,
            loc,
            makeMemberExpr(
                ctx->pool,
                loc,
                flgNone,
                makeTypedExpr(ctx->pool,
                              loc,
                              flgNone,
                              makeResolvedPath(ctx->pool,
                                               loc,
                                               S_this,
                                               node->flags,
                                               this,
                                               NULL,
                                               node->parentScope->type),
                              makeTypeReferenceNode(ctx->pool, type, loc),
                              NULL,
                              type),
                makeResolvedIdentifier(ctx->pool,
                                       loc,
                                       dctor->funcDecl.name,
                                       0,
                                       dctor,
                                       NULL,
                                       dctor->type),
                NULL,
                dctor->type),
            NULL,
            flgNone,
            NULL,
            dctor->type->func.retType),
        NULL,
        makeVoidType(ctx->types));
}

static AstNode *implementCallTupleDestructor(TypingContext *ctx,
                                             AstNode *member,
                                             const FileLoc *loc)
{
    const Type *type = member->type;
    const Type *destructor = type->tuple.destructorFunc;
    csAssert0(destructor);

    return makeExprStmt(
        ctx->pool,
        loc,
        flgNone,
        makeBackendCallExpr(
            ctx->pool,
            loc,
            flgNone,
            bfiDrop,
            makeResolvedPath(ctx->pool,
                             &member->loc,
                             member->structField.name,
                             member->flags | flgMember | flgAddThis,
                             member,
                             NULL,
                             type),
            destructor->func.retType),
        NULL,
        makeVoidType(ctx->types));
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
    return makeExprStmt(ctx->pool,
                        loc,
                        flgNone,
                        makeCallExpr(ctx->pool,
                                     loc,
                                     makeResolvedPath(ctx->pool,
                                                      loc,
                                                      S_DeinitOverload,
                                                      flgMember | flgAddThis,
                                                      (AstNode *)deinit_->decl,
                                                      NULL,
                                                      deinit_->type),
                                     NULL,
                                     flgNone,
                                     NULL,
                                     deinit_->type->func.retType),
                        NULL,
                        makeVoidType(ctx->types));
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
        if (!nodeIs(member, FieldDecl))
            continue;

        const Type *type = resolveAndUnThisType(member->type);
        AstNode *expr = NULL;
        if (isClassType(type)) {
            expr = makeFieldExpr(ctx->pool,
                                 &copy->loc,
                                 member->structField.name,
                                 member->flags,
                                 makeCopyReferenceCall(ctx, member, &copy->loc),
                                 member,
                                 NULL);
            refMembers = true;
        }
        else if (isStructType(type) && hasFlag(type, ReferenceMembers)) {
            const NamedTypeMember *copy_ =
                findStructMember(type, S_CopyOverload);
            csAssert0(copy_);
            expr = makeFieldExpr(
                ctx->pool,
                &copy->loc,
                member->structField.name,
                member->flags,
                implementCallStructCopyMember(ctx, member, &copy->loc),
                member,
                NULL);
            refMembers = true;
        }
        else if (isTupleType(type) && hasFlag(type, ReferenceMembers)) {
            expr =
                makeFieldExpr(ctx->pool,
                              &copy->loc,
                              member->structField.name,
                              member->flags,
                              implementCallTupleCopy(ctx, member, &copy->loc),
                              member,
                              NULL);
            refMembers = true;
        }
        else {
            expr = makeFieldExpr(
                ctx->pool,
                &copy->loc,
                member->structField.name,
                member->flags,
                makeResolvedPath(ctx->pool,
                                 &copy->loc,
                                 member->fieldExpr.name,
                                 member->flags | flgMember | flgAddThis,
                                 member,
                                 NULL,
                                 member->type),
                member,
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
                                        AstNode *destructor)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    csAssert0(nodeIs(node, StructDecl) || nodeIs(node, ClassDecl));
    csAssert0(nodeIs(destructor, FuncDecl) &&
              destructor->funcDecl.name == S_DestructorOverload &&
              destructor->funcDecl.body == NULL);

    AstNode *member = node->structDecl.members;
    AstNodeList stmts = {NULL};
    if (nodeIs(node, ClassDecl)) {
        AstNode *base = node->classDecl.base;
        if (base) {
            AstNode *callDctor = implementCallClassDestructor(
                ctx, destructor, base->type, &destructor->loc);
            if (callDctor)
                insertAstNode(&stmts, callDctor);
        }

        AstNode *callDeinit =
            implementCallClassDeinit(ctx, node, &destructor->loc);
        if (callDeinit) {
            node->flags |= flgImplementsDeinit;
            insertAstNode(&stmts, callDeinit);
        }
    }

    for (; member; member = member->next) {
        if (!nodeIs(member, FieldDecl) || hasFlag(member, Inherited))
            continue;

        const Type *type = member->type;
        AstNode *call = NULL;
        if (isStructType(type) && hasReferenceMembers(type)) {
            const NamedTypeMember *destructor_ =
                findStructMember(type, S_DestructorOverload);
            csAssert0(destructor_);
            call = implementCallStructDestructor(ctx, member, &destructor->loc);
        }
        else if (isTupleType(type) && hasReferenceMembers(type)) {
            call = implementCallTupleDestructor(ctx, member, &destructor->loc);
        }
        else if (isClassType(type)) {
            call = makeDropReferenceCall(ctx, member, &destructor->loc);
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
        destructor->funcDecl.body = makeBlockStmt(ctx->pool,
                                                  &destructor->loc,
                                                  stmts.first,
                                                  NULL,
                                                  makeVoidType(ctx->types));
    }
    else {
        destructor->funcDecl.body = makeBlockStmt(
            ctx->pool, &destructor->loc, NULL, NULL, makeVoidType(ctx->types));
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

    AstNode *dctor = findMemberDeclInType(node->type, S_DestructorOverload);
    if (dctor == NULL || !hasFlag(node, ReferenceMembers)) {
        fwd->funcDecl.body =
            makeBlockStmt(ctx->pool, &fwd->loc, NULL, NULL, NULL);
        return;
    }

    AstNode *type = makeResolvedPath(
        ctx->pool, &fwd->loc, S_This, flgNone, node, NULL, NULL);
    if (nodeIs(node, StructDecl)) {
        type =
            makePointerAstNode(ctx->pool, &fwd->loc, flgNone, type, NULL, NULL);
    }

    AstNode *call = makeCallExpr(
        ctx->pool,
        &fwd->loc,
        makeMemberExpr(
            ctx->pool,
            &fwd->loc,
            flgNone,
            makeTypedExpr(ctx->pool,
                          &fwd->loc,
                          flgNone,
                          makeResolvedPath(ctx->pool,
                                           &fwd->loc,
                                           S_ptr,
                                           fwd->flags,
                                           fwd->funcDecl.signature->params,
                                           NULL,
                                           NULL),
                          type,
                          NULL,
                          NULL),
            makeResolvedIdentifier(ctx->pool,
                                   &fwd->loc,
                                   dctor->funcDecl.name,
                                   0,
                                   dctor,
                                   NULL,
                                   NULL),
            NULL,
            NULL),
        NULL,
        flgNone,
        NULL,
        NULL);

    fwd->funcDecl.body = makeBlockStmt(
        ctx->pool,
        &fwd->loc,
        makeExprStmt(ctx->pool, &fwd->loc, flgNone, call, NULL, NULL),
        NULL,
        NULL);
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
            ctx->pool, &hash->loc, S_this, hash->flags, node, NULL, NULL),
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
        makeResolvedPathWithArgs(
            ctx->pool,
            &str->loc,
            getDeclarationName(toString),
            flgNone,
            toString,
            makeTypeReferenceNode(ctx->pool, node->type, &str->loc),
            NULL),
        makeResolvedPath(
            ctx->pool,
            &str->loc,
            S_this,
            str->flags,
            node,
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
                              AstNode *expr)
{
    return makeCallExpr(
        ctx->pool,
        &expr->loc,
        makeResolvedPath(ctx->pool,
                         &expr->loc,
                         slice->name,
                         flgNone,
                         slice->tStruct.decl,
                         NULL,
                         slice),
        makeResolvedPath(
            ctx->pool,
            &expr->loc,
            expr->varDecl.name,
            expr->flags,
            expr,
            makeIntegerLiteral(ctx->pool,
                               &expr->loc,
                               (i64)expr->type->array.len,
                               NULL,
                               getPrimitiveType(ctx->types, prtU64)),
            makePointerType(
                ctx->types, expr->type->array.elementType, flgNone)),
        flgNone,
        NULL,
        NULL);
}

AstNode *createClassOrStructBuiltins(MemPool *pool, AstNode *node)
{
    FileLoc loc = node->loc;
    loc.begin = loc.end;
    AstNodeList funcs = {NULL};
    if (!isBuiltinsInitialized())
        return NULL;

    if (nodeIs(node, StructDecl) && !hasMember(node, S_CopyOverload)) {
        insertAstNode(&funcs,
                      makeOperatorOverload(pool,
                                           &loc,
                                           opCopyOverload,
                                           NULL,
                                           NULL,
                                           NULL,
                                           flgPublic,
                                           NULL,
                                           NULL));
    }

    if (!hasMember(node, S_DestructorOverload)) {
        insertAstNode(&funcs,
                      makeOperatorOverload(
                          pool,
                          &loc,
                          opDestructorOverload,
                          NULL,
                          makeVoidAstNode(pool, &loc, flgNone, NULL, NULL),
                          NULL,
                          flgPublic,
                          NULL,
                          NULL));
    }

    csAssert0(!hasMember(node, S_DestructorFwd));
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
            flgPure | flgStatic | flgPublic,
            NULL,
            NULL));

    return funcs.first;
}

void implementClassOrStructBuiltins(AstVisitor *visitor, AstNode *node)
{
    AstNode *member = node->structDecl.members;
    for (; member; member = member->next) {
        if (!nodeIs(member, FuncDecl))
            continue;

        switch (member->funcDecl.operatorOverload) {
        case opDestructorOverload:
            node->flags |= flgImplementsDeinit;
            if (member->funcDecl.body == NULL)
                implementDestructorFunction(visitor, node, member);
            break;
        case opCopyOverload:
            if (member->funcDecl.body == NULL && nodeIs(node, StructDecl))
                implementStructCopyFunction(visitor, node, member);
            break;
        case opHashOverload:
            if (member->funcDecl.body == NULL)
                implementHashFunction(visitor, node, member);
            break;
        case opStringOverload:
            if (member->funcDecl.body == NULL)
                implementStringFunction(visitor, node, member);
            break;
        case opDestructorFwd:
            if (member->funcDecl.body == NULL)
                implementDestructorForwardFunction(visitor, node, member);
            break;
        default:
            break;
        }
    }
}

u64 removeClassOrStructBuiltins(AstNode *node, NamedTypeMember *nms)
{
    AstNodeList members = {};
    u64 count = 0, total = 0;
    for (AstNode *it = node->structDecl.members; it;) {
        AstNode *member = it;
        it = it->next;
        total++;
        member->next = NULL;
        if (nodeIs(member, FuncDecl)) {
            switch (member->funcDecl.operatorOverload) {
            case opDestructorOverload:
                node->flags |= flgImplementsDeinit;
                if (member->funcDecl.body == NULL)
                    continue;
                break;
            case opCopyOverload:
                if (member->funcDecl.body == NULL)
                    continue;
                break;
            case opDestructorFwd:
                if (member->funcDecl.body == NULL)
                    continue;
                break;
            default:
                break;
            }
        }
        insertAstNode(&members, member);
        nms[count] = nms[total - 1];
        count++;
    }

    node->structDecl.members = members.first;
    return total - count;
}
