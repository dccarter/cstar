//
// Created by Carter Mbotho on 2024-01-10.
//

#include "simplify.h"
#include "driver/driver.h"
#include "lang/frontend/ast.h"
#include "lang/frontend/capture.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/visitor.h"
#include "lang/middle/builtins.h"

typedef struct {
    AstNode *node;
    AstNode *target;
} NodeToExternDecl;

typedef struct SimplifyContext {
    Log *L;
    TypeTable *types;
    StrPool *strings;
    MemPool *pool;
    AstModifier root;
    HashTable n2e;
    AstNodeList init;
    AstNodeList *startup;
    union {
        struct {
            AstNode *currentFunction;
            AstModifier block;
        };
        struct {
            AstNode *currentFunction;
            AstModifier block;
        } stack;
    };
} SimplifyContext;

static bool isRedundantExpression(AstNode *node);

static bool isRedundantStatement(AstNode *node);

static bool compareNodeToExternDecl(const void *lhs, const void *rhs)
{
    return ((NodeToExternDecl *)lhs)->node == ((NodeToExternDecl *)rhs)->node;
}

static bool isVirtualDispatch(const AstNode *target, const AstNode *member)
{
    if (!isClassOrStructType(target->type))
        return false;
    AstNode *decl = getTypeDecl(target->type),
            *func = getTypeDecl(member->type);

    return hasFlag(decl, Virtual) && hasFlag(func, Virtual);
}

static AstNode *virtualDispatch(SimplifyContext *ctx,
                                AstNode *target,
                                AstNode *member)
{
    // [target].vtable.[member](target, [args])
    AstNode *decl = getTypeDecl(target->type),
            *vTable = decl->classDecl.members,
            *vTableFunc = findMemberDeclInType(vTable->type->pointer.pointed,
                                               member->ident.value);
    return makeMemberExpr(ctx->pool,
                          &target->loc,
                          flgConst,
                          makeMemberExpr(ctx->pool,
                                         &target->loc,
                                         flgConst,
                                         deepCloneAstNode(ctx->pool, target),
                                         makeResolvedIdentifier(ctx->pool,
                                                                &target->loc,
                                                                S_vtable,
                                                                0,
                                                                vTable,
                                                                NULL,
                                                                vTable->type),
                                         NULL,
                                         vTable->type),
                          makeResolvedIdentifier(ctx->pool,
                                                 &target->loc,
                                                 vTableFunc->_namedNode.name,
                                                 0,
                                                 vTableFunc,
                                                 NULL,
                                                 vTableFunc->type),
                          NULL,
                          vTableFunc->type);
}

static void addNodeToExternDecl(SimplifyContext *ctx,
                                AstNode *node,
                                AstNode *target)
{
    if (nodeIs(node, FuncDecl) ||
        (hasFlag(node, TopLevelDecl) && nodeIs(node, VarDecl))) //
    {
        insertInHashTable(
            &ctx->n2e,
            &(NodeToExternDecl){.node = node, .target = target ?: node},
            hashPtr(hashInit(), node),
            sizeof(NodeToExternDecl),
            compareNodeToExternDecl);
    }
}

static NodeToExternDecl *getNodeToExternDecl(SimplifyContext *ctx,
                                             AstNode *decl)
{
    return findInHashTable(&ctx->n2e,
                           &(NodeToExternDecl){.node = decl, .target = decl},
                           hashPtr(hashInit(), decl),
                           sizeof(NodeToExternDecl),
                           compareNodeToExternDecl);
}

static AstNode *makeExternReferenceToBuiltin(SimplifyContext *ctx,
                                             cstring builtin)
{
    AstNode *target = findBuiltinDecl(builtin);
    csAssert0(target);
    AstNode *decl = makeAstNode(ctx->pool,
                                &target->loc,
                                &(AstNode){.tag = astExternDecl,
                                           .type = target->type,
                                           .flags = target->flags,
                                           .externDecl.func = target});
    addNodeToExternDecl(ctx, target, decl);
    return decl;
}

static void simplifyForRangeStmt(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *range = node->forStmt.range, *var = node->forStmt.var,
            *body = node->forStmt.body;

    // var x = range.start
    var->varDecl.init = range->rangeExpr.start;

    // while (x < range.end) { ...; x += range.step; }
    var->next = makeWhileStmt(
        ctx->pool,
        &node->loc,
        node->flags,
        // x < range.end
        makeBinaryExpr(ctx->pool,
                       &range->loc,
                       flgNone,
                       // x
                       makeResolvedIdentifier(ctx->pool,
                                              &range->loc,
                                              var->varDecl.names->ident.value,
                                              0,
                                              var,
                                              NULL,
                                              var->type),
                       opNe,
                       // range.end
                       range->rangeExpr.end,
                       NULL,
                       getPrimitiveType(ctx->types, prtBool)),
        // ...
        body,
        NULL,
        // x += range.step
        makeExprStmt(
            ctx->pool,
            &node->loc,
            flgNone,
            // x += range.step
            makeAssignExpr(
                ctx->pool,
                &range->loc,
                flgNone,
                // x
                makeResolvedIdentifier(ctx->pool,
                                       &range->loc,
                                       var->varDecl.name,
                                       0,
                                       var,
                                       NULL,
                                       var->type),
                opAdd,
                // range.step
                range->rangeExpr.step
                    ?: makeIntegerLiteral(
                           ctx->pool, &range->loc, 1, NULL, range->type),
                NULL,
                range->type),
            NULL,
            range->type));

    node->tag = astBlockStmt;
    node->blockStmt.stmts = var;
    node->blockStmt.last = var->next;
    astVisit(visitor, node);
}

static void simplifyForArrayStmt(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *range = node->forStmt.range, *var = node->forStmt.var,
            *body = node->forStmt.body;
    const Type *elem = typeIs(range->type, String)
                           ? getPrimitiveType(ctx->types, prtChar)
                           : range->type->array.elementType;

    AstNode *index =
        var->next
            ?: makeVarDecl(
                   ctx->pool,
                   &var->loc,
                   flgConst,
                   makeAnonymousVariable(ctx->strings, "i"),
                   NULL,
                   makeIntegerLiteral(ctx->pool,
                                      &var->loc,
                                      0,
                                      NULL,
                                      getPrimitiveType(ctx->types, prtU64)),
                   NULL,
                   getPrimitiveType(ctx->types, prtU64));
    if (var->next) {
        var->next->varDecl.init =
            makeIntegerLiteral(ctx->pool,
                               &var->loc,
                               0,
                               NULL,
                               getPrimitiveType(ctx->types, prtU64));
    }
    else {
        var->next = index;
    }

    range = makeVarDecl(ctx->pool,
                        &range->loc,
                        range->flags,
                        "",
                        NULL,
                        range,
                        NULL,
                        range->type);
    index->next = range;

    AstNode *condition = makeBinaryExpr(
        ctx->pool,
        &range->loc,
        flgNone,
        makeResolvedIdentifier(ctx->pool,
                               &range->loc,
                               index->varDecl.name,
                               0,
                               index,
                               NULL,
                               index->type),
        opLt,
        makeUnsignedIntegerLiteral(ctx->pool,
                                   &range->loc,
                                   range->type->array.len,
                                   NULL,
                                   getPrimitiveType(ctx->types, prtU64)),
        NULL,
        getPrimitiveType(ctx->types, prtBool));

    AstNode *assign = makeExprStmt(
        ctx->pool,
        &range->loc,
        flgNone,
        makeAssignExpr(ctx->pool,
                       &range->loc,
                       flgNone,
                       makeResolvedIdentifier(ctx->pool,
                                              &range->loc,
                                              var->varDecl.name,
                                              0,
                                              var,
                                              NULL,
                                              var->type),
                       opAssign,
                       makeIndexExpr(ctx->pool,
                                     &range->loc,
                                     elem->flags,
                                     makeResolvedIdentifier(ctx->pool,
                                                            &range->loc,
                                                            range->varDecl.name,
                                                            0,
                                                            range,
                                                            NULL,
                                                            range->type),
                                     makeResolvedIdentifier(ctx->pool,
                                                            &range->loc,
                                                            index->varDecl.name,
                                                            flgNone,
                                                            index,
                                                            NULL,
                                                            index->type),
                                     NULL,
                                     var->type),
                       NULL,
                       range->type),
        NULL,
        var->type);

    AstNode *advance = makeExprStmt(
        ctx->pool,
        &node->loc,
        flgNone,
        makeAssignExpr(
            ctx->pool,
            &range->loc,
            flgNone,
            makeResolvedIdentifier(ctx->pool,
                                   &range->loc,
                                   index->varDecl.name,
                                   0,
                                   index,
                                   NULL,
                                   index->type),
            opAdd,
            makeIntegerLiteral(ctx->pool, &range->loc, 1, NULL, index->type),
            NULL,
            index->type),
        NULL,
        index->type);
    assign->next = advance;

    if (nodeIs(body, BlockStmt)) {
        if (body->blockStmt.stmts) {
            assign->next = body->blockStmt.stmts;
            body->blockStmt.stmts = assign;
        }
        else
            body->blockStmt.stmts = assign;
    }
    else {
        assign->next = body;
        body = makeBlockStmt(ctx->pool, &assign->loc, assign, NULL, node->type);
    }

    range->next = makeWhileStmt(ctx->pool,
                                &node->loc,
                                node->flags,
                                condition,
                                body,
                                NULL,
                                deepCloneAstNode(ctx->pool, advance));
    node->tag = astBlockStmt;
    node->blockStmt.stmts = var;
    node->blockStmt.last = range->next;
    astVisit(visitor, node);
}

static void simplifyCastExpression(SimplifyContext *ctx,
                                   AstNode *node,
                                   const Type *type)
{
    const Type *from = unwrapType(node->type, NULL);
    if (type == from || typeIs(type, Func))
        return;

    if (!typeIs(from, Union)) {
        AstNode *expr = deepCloneAstNode(ctx->pool, node);
        if (typeIs(from, Array)) {
            node->tag = astTypedExpr;
            node->typedExpr.expr = expr;
            node->typedExpr.type =
                makeTypeReferenceNode(ctx->pool, type, &node->loc);
        }
        else {
            node->tag = astCastExpr;
            node->castExpr.expr = expr;
            node->castExpr.to =
                makeTypeReferenceNode(ctx->pool, type, &node->loc);
        }
        node->type = type;
    }
}

static void simplifyCondition(SimplifyContext *ctx, AstNode *cond)
{
    AstNode *rhs = NULL, *lhs = deepCloneAstNode(ctx->pool, cond);
    if (isIntegerType(cond->type)) {
        rhs = makeIntegerLiteral(ctx->pool, &cond->loc, 0, NULL, cond->type);
    }
    else if (isFloatType(cond->type)) {
        rhs = makeFloatLiteral(ctx->pool, &cond->loc, 0.0, NULL, cond->type);
    }
    else if (isCharacterType(cond->type)) {
        rhs = makeCharLiteral(ctx->pool, &cond->loc, 0, NULL, cond->type);
    }
    else if (isPointerType(cond->type)) {
        rhs = makeNullLiteral(ctx->pool, &cond->loc, NULL, cond->type);
    }
    else {
        unreachable("Shouldn't be a thing!");
    }
    cond->tag = astBinaryExpr;
    cond->binaryExpr.op = opNe;
    cond->binaryExpr.lhs = lhs;
    cond->binaryExpr.rhs = rhs;
}

static bool isRedundantExpressionMany(AstNode *node)
{
    AstNode *it = node;
    for (; it; it = it->next) {
        if (isRedundantExpression(it))
            return true;
    }

    return false;
}

static bool isRedundantStructExpression(AstNode *node)
{
    AstNode *it = node->structExpr.fields;
    for (; it; it = it->next) {
        if (isRedundantExpression(it->fieldExpr.value))
            return true;
    }

    return false;
}

static bool isRedundantExpression(AstNode *node)
{
    if (node == NULL)
        return true;

    if (hasFlag(node, BlockValue))
        return false;

    switch (node->tag) {
    case astNullLit:
    case astCharLit:
    case astBoolLit:
    case astIntegerLit:
    case astFloatLit:
    case astStringLit:
    case astIdentifier:
    case astPath:
        return true;
    case astMemberExpr:
        return isRedundantExpression(node->memberExpr.target);
    case astIndexExpr:
        return isRedundantExpression(node->indexExpr.target);
    case astArrayExpr:
        return isRedundantExpressionMany(node->arrayExpr.elements);
    case astTupleExpr:
        return isRedundantExpressionMany(node->tupleExpr.elements);
    case astStructExpr:
        return isRedundantStructExpression(node);
    case astGroupExpr:
        return isRedundantExpression(node->groupExpr.expr);
    case astStmtExpr:
        return isRedundantStatement(node->stmtExpr.stmt);
    case astUnaryExpr: {
        Operator op = node->unaryExpr.op;
        return op != opPreInc && op != opPreDec && op != opPostInc &&
               op != opPostDec &&
               isRedundantExpression(node->unaryExpr.operand);
    }

    case astBinaryExpr:
        return isRedundantExpression(node->binaryExpr.lhs) ||
               isRedundantExpression(node->binaryExpr.rhs);

    case astTernaryExpr:
        return isRedundantExpression(node->ternaryExpr.cond) ||
               isRedundantExpression(node->ternaryExpr.body) ||
               isRedundantExpression(node->ternaryExpr.otherwise);
    default:
        return false;
    }
}

bool isRedundantStatement(AstNode *node)
{
    if (nodeIs(node, BlockStmt))
        return node->blockStmt.stmts == NULL;

    if (!nodeIs(node, ExprStmt))
        return false;

    AstNode *expr = node->exprStmt.expr;
    if (nodeIs(expr, ExprStmt))
        return isRedundantStatement(expr);

    return isRedundantExpression(expr);
}

static void visitProgram(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *decl = node->program.decls;
    AstNodeList externals = {};

    astVisit(visitor, node->program.module);
    astVisitManyNodes(visitor, node->program.top);

    astModifierInit(&ctx->root, node);

    if (isBuiltinsInitialized()) {
        insertAstNode(&externals,
                      makeExternReferenceToBuiltin(ctx, S_sptr_ref));
        insertAstNode(&externals,
                      makeExternReferenceToBuiltin(ctx, S_sptr_drop));
    }

    for (; decl; decl = decl->next) {
        astModifierNext(&ctx->root, decl);
        if (nodeIsNoop(decl)) {
            astModifierRemoveCurrent(&ctx->root);
            continue;
        }

        astVisit(visitor, decl);
    }

    if (isBuiltinsInitialized()) {
        externals.last->next = node->program.decls;
        node->program.decls = externals.first;
    }
}

static void visitCallExpr(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *callee = node->callExpr.callee, *args = node->callExpr.args;
    AstNode *func = callee->type->func.decl;

    astVisit(visitor, callee);
    AstNode *arg = args,
            *params = nodeIs(func, FuncDecl) ? func->funcDecl.signature->params
                                             : func->funcType.params,
            *param = params;
    for (u64 i = 0; arg && param; arg = arg->next, i++, param = param->next) {
        if (hasFlag(param, Variadic))
            break;
        if (typeIs(func->type->func.params[i], Auto))
            continue;

        const Type *left = unwrapType(func->type->func.params[i], NULL),
                   *right = unwrapType(arg->type, NULL);
        if (left != right) {
            simplifyCastExpression(ctx, arg, left);
        }
    }

    astVisitManyNodes(visitor, args);

    csAssert0(func);
    AstNode *this = getMemberFunctionThis(func);
    if (this == NULL || !nodeIs(callee, MemberExpr))
        return;

    csAssert0(nodeIs(callee, MemberExpr));
    AstNode *target = callee->memberExpr.target,
            *call = callee->memberExpr.member;

    call->type = callee->type;

    if (!typeIs(target->type, Pointer) && !isClassType(target->type)) {
        target = makeAddrOffExpr(
            ctx->pool, &target->loc, this->flags, target, NULL, this->type);
    }
    target->flags |= flgTransient;
    if (isVirtualDispatch(target, call)) {
        node->callExpr.callee = virtualDispatch(ctx, target, call);
        target->next = args;
        node->callExpr.args = target;
    }
    else {
        target->next = args;
        node->callExpr.args = target;
        node->callExpr.callee = call;
    }
}

static void visitIdentifier(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *target = node->ident.resolvesTo;
    if (nodeIs(target, FuncDecl) ||
        (nodeIs(target, VarDecl) && hasFlag(target, TopLevelDecl))) //
    {
        NodeToExternDecl *f2e = getNodeToExternDecl(ctx, target);
        if (f2e == NULL) {
            // new function declaration added, add extern
            AstNode *decl = makeAstNode(ctx->pool,
                                        &target->loc,
                                        &(AstNode){.tag = astExternDecl,
                                                   .type = target->type,
                                                   .flags = target->flags,
                                                   .externDecl.func = target});

            addNodeToExternDecl(ctx, target, decl);
            astModifierAdd(&ctx->root, decl);
            node->ident.resolvesTo = decl;
        }
        else {
            node->ident.resolvesTo = f2e->target;
        }
    }
}

static void visitPathElement(AstVisitor *visitor, AstNode *node)
{
    AstNode copy = *node;
    node->tag = astIdentifier;
    node->ident.value = copy.pathElement.name;
    node->ident.resolvesTo = copy.pathElement.resolvesTo;
    node->ident.super = copy.pathElement.super;
    node->next = NULL;
    astVisit(visitor, node);
}

static void visitPathExpr(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    if (hasFlag(node, AddThis)) {
        AstNode *this = getMemberFunctionThis(ctx->currentFunction);
        csAssert0(this);
        AstNode *base = node->path.elements;
        node->path.elements = makeResolvedPathElement(
            ctx->pool, &node->loc, S_this, flgNone, this, base, this->type);

        if (base->pathElement.resolvesTo == NULL) {
            // it already resolves to correct member
            base->pathElement.resolvesTo = findMemberDeclInType(
                stripAll(this->type), base->pathElement.name);
            csAssert0(base->pathElement.resolvesTo);
        }
        node->flags &= ~flgAddThis;
    }

    AstNode *elem = node->path.elements;
    AstNode *resolved = elem->pathElement.resolvesTo;
    if (hasFlag(resolved, Extern) && nodeIs(resolved, ImportDecl))
        elem = elem->next;

    AstNode *next = elem->next;
    astVisit(visitor, elem);
    if (next == NULL) {
        replaceAstNodeWith(node, elem);
        return;
    }

    AstNode *target = elem;
    for (; next;) {
        elem = next;
        next = next->next;
        astVisit(visitor, elem);
        if (next) {
            target->next = NULL;
            elem->next = NULL;
            target = makeMemberExpr(ctx->pool,
                                    locExtend(&target->loc, &elem->loc),
                                    elem->flags,
                                    target,
                                    elem,
                                    NULL,
                                    elem->type);
        }
    }

    node->tag = astMemberExpr;
    node->memberExpr.target = target;
    node->memberExpr.member = elem;
}

static void visitStmtExpr(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    astVisit(visitor, node->stmtExpr.stmt);
    AstNode *stmt = node->stmtExpr.stmt;
    if (nodeIs(stmt, BlockStmt)) {
        if (stmt->blockStmt.stmts == NULL) {
            astModifierRemoveCurrent(&ctx->block);
        }
        else if (stmt->blockStmt.stmts->next == NULL) {
            replaceAstNode(node, stmt->blockStmt.stmts);
            node->flags &= ~flgBlockValue;
        }
    }
}

static void visitStructDecl(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNodeList fields = {NULL}, others = {NULL};
    u64 i = 0;

    AstNode *member = node->structDecl.members, *next = member;
    for (; next;) {
        member = next;
        next = next->next;
        member->next = NULL;
        if (nodeIs(member, FieldDecl)) {
            member->fieldExpr.index = i++;
            insertAstNode(&fields, member);
            continue;
        }

        if (nodeIs(member, FuncDecl)) {
            insertAstNode(&others, member);
        }
    }
    node->structDecl.members = fields.first;
    if (others.first)
        astModifierAddAsNext(&ctx->root, others.first);
    // node->tag = astStructDecl;
}

static void visitForStmt(AstVisitor *visitor, AstNode *node)
{
    AstNode *range = node->forStmt.range;
    if (nodeIs(range, RangeExpr))
        simplifyForRangeStmt(visitor, node);
    else
        simplifyForArrayStmt(visitor, node);
}

static void visitBlockStmt(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    astModifierInit(&ctx->block, node);
    AstNode *stmt = node->blockStmt.stmts, *last = node->blockStmt.last;

    for (; stmt; stmt = stmt->next) {
        astModifierNext(&ctx->block, stmt);
        if (hasFlag(stmt, Comptime) || nodeIsNoop(stmt)) {
            astModifierRemoveCurrent(&ctx->block);
            continue;
        }
        astVisit(visitor, stmt);
        if (!hasFlag(node, BlockReturns) || stmt->next != NULL) {
            if (isRedundantStatement(stmt)) {
                logWarningWithId(
                    ctx->L,
                    wrnRedundantStmt,
                    &stmt->loc,
                    "removing this statement because it is redundant",
                    NULL);
                astModifierRemoveCurrent(&ctx->block);
                continue;
            }
        }
        last = stmt;
    }

    if (last && hasFlag(node, BlockReturns))
        last->flags |= flgBlockValue;

    node->blockStmt.last = last;
}

void visitIfStmt(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);

    AstNode *cond = node->ifStmt.cond;
    if (!isBooleanType(cond->type)) {
        simplifyCondition(ctx, cond);
    }
    astVisitFallbackVisitAll(visitor, node);
}

static void visitWhileStmt(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);

    AstNode *cond = node->whileStmt.cond;
    if (!isBooleanType(cond->type)) {
        simplifyCondition(ctx, cond);
    }
    astVisitFallbackVisitAll(visitor, node);
}

static void visitFuncDecl(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    ctx->currentFunction = node;
    addNodeToExternDecl(ctx, node, node);
    if (node->funcDecl.this_) {
        node->funcDecl.signature->params = node->funcDecl.this_;
    }

    astVisitFallbackVisitAll(visitor, node);
    ctx->currentFunction = NULL;
}

static void visitVarDecl(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    addNodeToExternDecl(ctx, node, node);
    astVisitFallbackVisitAll(visitor, node);
    AstNode *init = node->varDecl.init;
    if (!hasFlag(node, TopLevelDecl) || init == NULL) {
        return;
    }
    else if (nodeIs(init, CallExpr)) {
        if (typeIs(init->type, Class)) {
            node->varDecl.init =
                makeNullLiteral(ctx->pool, &init->loc, NULL, init->type);
        }
        else {
            node->varDecl.init = NULL;
        }
        // just a list of assignment expressions
        insertAstNode(&ctx->init,
                      makeAssignExpr(ctx->pool,
                                     &node->loc,
                                     node->flags,
                                     makeResolvedIdentifier(ctx->pool,
                                                            &node->loc,
                                                            node->varDecl.name,
                                                            0,
                                                            node,
                                                            NULL,
                                                            node->type),
                                     opAssign,
                                     init,
                                     NULL,
                                     init->type));
    }
}

static void createModuleInit(SimplifyContext *ctx, AstNode *program)
{
    cstring name = makeString(ctx->strings, S___init);
    AstNode *func = makeFunctionDecl(
        ctx->pool,
        builtinLoc(),
        name,
        NULL,
        makeVoidAstNode(
            ctx->pool, builtinLoc(), flgNone, NULL, makeVoidType(ctx->types)),
        makeBlockStmt(ctx->pool, builtinLoc(), ctx->init.first, NULL, NULL),
        flgTopLevelDecl | flgPublic,
        NULL,
        NULL);

    func->type = makeFuncType(
        ctx->types,
        &(Type){.tag = typFunc,
                .name = name,
                .func = {.retType = makeVoidType(ctx->types), .decl = func}});
    func->parentScope = program;

    AstNode *ext =
        makeAstNode(ctx->pool,
                    &func->loc,
                    &(AstNode){.tag = astExternDecl,
                               .type = func->type,
                               .flags = func->flags | flgPublic | flgModuleInit,
                               .next = program->program.decls,
                               .externDecl.func = func});
    program->program.decls = ext;
    getLastAstNode(ext)->next = func;
}

static void simplifyMainModule(SimplifyContext *ctx, AstNode *program)
{
    const Type *moduleCtorType = makeTupleType(
        ctx->types,
        (const Type *[]){
            getPrimitiveType(ctx->types, prtI32),
            makeFuncType(ctx->types,
                         &(Type){.tag = typFunc,
                                 .name = S___init,
                                 .func = {.retType = makeVoidType(ctx->types),
                                          .decl = NULL}}),
            makeVoidPointerType(ctx->types, flgNone)},
        3,
        flgNone);

    AstNodeList elems = {};
    AstNode *init = ctx->startup->first;
    i32 i = 0;
    for (; init; init = init->next, i++) {
        // we want to add that function here
        AstNode *members = makeIntegerLiteral(
            ctx->pool,
            &init->loc,
            i,
            makeResolvedIdentifier(
                ctx->pool,
                &init->loc,
                init->type->name,
                0,
                init,
                makeNullLiteral(ctx->pool,
                                &init->loc,
                                NULL,
                                moduleCtorType->tuple.members[2]),
                init->type),
            moduleCtorType->tuple.members[0]);
        insertAstNode(&elems,
                      makeTupleExpr(ctx->pool,
                                    &members->loc,
                                    flgNone,
                                    members,
                                    NULL,
                                    moduleCtorType));
    }

    getLastAstNode(ctx->startup->first)->next = program->program.decls;
    program->program.decls = ctx->startup->first;
    const Type *type = makeArrayType(ctx->types, moduleCtorType, i);
    ctx->startup->first = NULL;

    AstNode *ctors = makeVarDecl(
        ctx->pool,
        builtinLoc(),
        flgTopLevelDecl | flgConst | flgDiDisable,
        makeString(ctx->strings, S___LLVM_global_ctors),
        NULL,
        makeArrayExpr(
            ctx->pool, builtinLoc(), flgConst, elems.first, NULL, type),
        NULL,
        type);
    ctors->attrs = makeAttribute(
        ctx->pool,
        builtinLoc(),
        S_linkage,
        makeStringLiteral(ctx->pool,
                          builtinLoc(),
                          S_Appending,
                          NULL,
                          makeStringType(ctx->types)),
        makeAttribute(ctx->pool,
                      builtinLoc(),
                      S_section,
                      makeStringLiteral(ctx->pool,
                                        builtinLoc(),
                                        S_ctor_section,
                                        NULL,
                                        makeStringType(ctx->types)),
                      NULL));
    getLastAstNode(program->program.decls)->next = ctors;
}

static void withSavedStack(Visitor func, AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    __typeof(ctx->stack) stack = ctx->stack;

    func(visitor, node);

    ctx->stack = stack;
}

static AstNode *simplifyCode(CompilerDriver *driver, AstNode *node)
{
    SimplifyContext context = {.L = driver->L,
                               .types = driver->types,
                               .strings = driver->strings,
                               .pool = driver->pool,
                               .startup = &driver->startup,
                               .n2e = newHashTable(sizeof(NodeToExternDecl))};

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context, {
        [astProgram] = visitProgram,
        [astPath] = visitPathExpr,
        [astPathElem] = visitPathElement,
        [astIdentifier] = visitIdentifier,
        [astCallExpr] = visitCallExpr,
        [astStmtExpr] = visitStmtExpr,
        [astForStmt] = visitForStmt,
        [astBlockStmt] = visitBlockStmt,
        [astWhileStmt] = visitWhileStmt,
        [astIfStmt] = visitIfStmt,
        [astStructDecl] = visitStructDecl,
        [astClassDecl] = visitStructDecl,
        [astFuncDecl] = visitFuncDecl,
        [astVarDecl] = visitVarDecl,
        [astGenericDecl] = astVisitSkip,
        [astMacroDecl] = astVisitSkip
    }, .fallback = astVisitFallbackVisitAll, .dispatch = withSavedStack);
    // clang-format on

    astVisit(&visitor, node);
    freeHashTable(&context.n2e);
    if (context.init.first != NULL) {
        // only create initializer when there is data to initialize
        createModuleInit(&context, node);
    }

    if (hasFlag(node, Main) && driver->startup.first) {
        simplifyMainModule(&context, node);
    }
    return node;
}

AstNode *simplifyAst(CompilerDriver *driver, AstNode *node)
{
    node = simplifyDeferStatements(driver, node);
    if (node == NULL || hasErrors(driver->L))
        return node;

    return simplifyCode(driver, node);
}
