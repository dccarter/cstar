//
// Created by Carter Mbotho on 2024-01-10.
//

#include "../defer.h"
#include "../n2e.h"

#include "driver/driver.h"
#include "lang/frontend/ast.h"
#include "lang/frontend/capture.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/visitor.h"
#include "lang/middle/builtins.h"

typedef struct SimplifyContext {
    Log *L;
    TypeTable *types;
    StrPool *strings;
    MemPool *pool;
    AstModifier root;
    AstModifier block;
    N2eContext n2e;
    AstNodeList init;
    AstNodeList *startup;
    AstNode *returnVar;
    bool traceMemory;
    union {
        struct {
            AstNode *currentFunction;
        };
        struct {
            AstNode *currentFunction;
        } stack;
    };
} SimplifyContext;

static bool isRedundantExpression(AstNode *node);

static bool isRedundantStatement(AstNode *node);

static bool compareNodeToExternDecl(const void *lhs, const void *rhs)
{
    return ((NodeToExternDecl *)lhs)->node == ((NodeToExternDecl *)rhs)->node;
}

static inline bool nodeIsComplexExpr(const AstNode *node)
{
    return !nodeIsLeftValue(node) && !isLiteralExprExt(node) &&
           !isSizeofExpr(node);
}

static bool nodeNeedsTemporaryVar(const AstNode *node)
{
    if (nodeIs(node, PointerOf) || nodeIs(node, ReferenceOf))
        return nodeNeedsTemporaryVar(node->unaryExpr.operand);

    if (nodeIsLeftValue(node) || isLiteralExprExt(node) || isSizeofExpr(node))
        return false;

    if (nodeIs(node, UnaryExpr))
        return node->unaryExpr.op == opMove &&
               nodeNeedsTemporaryVar(node->unaryExpr.operand);
    return true;
}

static bool isVirtualDispatch(const AstNode *target, const AstNode *member)
{
    const Type *type = stripReference(target->type);
    if (!isClassOrStructType(type))
        return false;
    AstNode *decl = getTypeDecl(type), *func = getTypeDecl(member->type);

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
    AstNode *base = NULL;
    if (nodeIs(target, ReferenceOf)) {
        base = deepCloneAstNode(ctx->pool, target->unaryExpr.operand);
    }
    else {
        base = deepCloneAstNode(ctx->pool, target);
    }

    return makeMemberExpr(ctx->pool,
                          &target->loc,
                          flgConst,
                          makeMemberExpr(ctx->pool,
                                         &target->loc,
                                         flgConst,
                                         base,
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

static void simplifyForRangeStmt(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *range = node->forStmt.range, *var = node->forStmt.var,
            *body = node->forStmt.body;

    // var x = range.start
    var->varDecl.init = range->rangeExpr.start;
    var->parentScope = body;
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
    body->parentScope = var->next;
    node->tag = astBlockStmt;
    node->flags = flgScoping;
    node->blockStmt.stmts = var;
    node->blockStmt.last = var->next;
    astVisit(visitor, node);
}

static void simplifyForArrayStmt(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *range = node->forStmt.range, *var = node->forStmt.var,
            *body = node->forStmt.body;
    const Type *rangeType = unwrapType(range->type, NULL);
    const Type *elem = typeIs(rangeType, String)
                           ? getPrimitiveType(ctx->types, prtCChar)
                           : rangeType->array.elementType;

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
                        makeAnonymousVariable(ctx->strings, "_for"),
                        NULL,
                        range,
                        NULL,
                        typeIs(rangeType, String)
                            ? range->type
                            : makePointerType(ctx->types, elem, flgNone));
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
                                   rangeType->array.len,
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

#if defined(LLVM_BACKEND)
    if ((type == from) || typeIs(type, Func))
        return;
#else
    if ((type == from) && !typeIs(type, Func))
        return;
#endif

    if (typeIs(from, Union)) {
        u32 idx = findUnionTypeIndex(from, type);
        if (idx == UINT32_MAX) {
            logError(ctx->L,
                     &node->loc,
                     "cannot cast type {t} to union type {t}",
                     (FormatArg[]){{.t = type}, {.t = from}});
            return;
        }

        node->memberExpr.target = deepCloneAstNode(ctx->pool, node);
        node->tag = astMemberExpr;
        node->memberExpr.member =
            makeIntegerLiteral(ctx->pool,
                               &node->loc,
                               idx,
                               NULL,
                               getPrimitiveType(ctx->types, prtU64));
        node->type = type;
    }
    else {
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
               op != opPostDec && op != opDelete &&
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

static void createModuleInit(AstVisitor *visitor, AstNode *program)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    cstring name =
        program->program.module
            ? makeStringConcat(ctx->strings,
                               S___init,
                               "_",
                               program->program.module->moduleDecl.name)
            : S___init;
    AstNode *func = makeFunctionDecl(
        ctx->pool,
        builtinLoc(),
        name,
        NULL,
        makeVoidAstNode(
            ctx->pool, builtinLoc(), flgNone, NULL, makeVoidType(ctx->types)),
        makeBlockStmt(ctx->pool, builtinLoc(), ctx->init.first, NULL, NULL),
        flgTopLevelDecl | flgPublic | flgConstructor,
        NULL,
        NULL);

    astVisit(visitor, func->funcDecl.body);

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
                      n2eMakeExternReferenceToBuiltin(&ctx->n2e, S_sptr_ref));
        insertAstNode(&externals,
                      n2eMakeExternReferenceToBuiltin(&ctx->n2e, S_sptr_drop));

        if (ctx->traceMemory) {
            insertAstNode(
                &externals,
                n2eMakeExternReferenceToBuiltin(&ctx->n2e, S_sptr_get_trace));
            insertAstNode(
                &externals,
                n2eMakeExternReferenceToBuiltin(&ctx->n2e, S_sptr_drop_trace));
        }
    }

    for (; decl; decl = decl->next) {
        astModifierNext(&ctx->root, decl);
        if (nodeIsNoop(decl)) {
            astModifierRemoveCurrent(&ctx->root);
            continue;
        }

        astVisit(visitor, decl);
    }

    if (ctx->init.first != NULL) {
        // only create initializer when there is data to initialize
        createModuleInit(visitor, node);
    }

    if (isBuiltinsInitialized()) {
        externals.last->next = node->program.decls;
        node->program.decls = externals.first;
    }
}

static void visitCastExpr(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->castExpr.expr, *to = node->castExpr.to;
    astVisit(visitor, expr);

    if (isUnionType(to->type)) {
        const Type *type = stripReference(expr->type);
        if (to->type == type) {
            node->tag = astBackendCall;
            node->backendCallExpr.func = bfiCopy;
            node->backendCallExpr.args = expr;
        }
        else {
            u32 idx = findUnionTypeIndex(to->type, type);
            csAssert0(idx != UINT32_MAX);
            node->tag = astUnionValueExpr;
            node->unionValue.idx = idx;
            node->unionValue.value = expr;
        }
    }
    else if (isUnionType(expr->type) && !hasFlag(node, BitUnionCast)) {
        AstNode *builtinAssert = findBuiltinDecl(S___cxy_assert);
        csAssert0(builtinAssert);
        u32 idx = findUnionTypeIndex(expr->type, stripReference(to->type));
        csAssert0(idx != UINT32_MAX);
        AstNodeList args = {};
        insertAstNode(
            &args,
            makeBinaryExpr(
                ctx->pool,
                &expr->loc,
                flgNone,
                makeMemberExpr(ctx->pool,
                               &expr->loc,
                               flgNone,
                               deepCloneAstNode(ctx->pool, expr),
                               makeUnsignedIntegerLiteral(
                                   ctx->pool,
                                   &expr->loc,
                                   0,
                                   NULL,
                                   getPrimitiveType(ctx->types, prtU64)),
                               NULL,
                               getPrimitiveType(ctx->types, prtU64)),
                opEq,
                makeUnsignedIntegerLiteral(
                    ctx->pool,
                    &expr->loc,
                    idx,
                    NULL,
                    getPrimitiveType(ctx->types, prtU64)),
                NULL,
                getPrimitiveType(ctx->types, prtBool)));
        insertAstNode(&args,
                      makeStringLiteral(ctx->pool,
                                        &expr->loc,
                                        expr->loc.fileName,
                                        NULL,
                                        makeStringType(ctx->types)));
        insertAstNode(
            &args,
            makeUnsignedIntegerLiteral(ctx->pool,
                                       &expr->loc,
                                       expr->loc.begin.row,
                                       NULL,
                                       getPrimitiveType(ctx->types, prtU64)));
        insertAstNode(
            &args,
            makeUnsignedIntegerLiteral(ctx->pool,
                                       &expr->loc,
                                       expr->loc.begin.col,
                                       NULL,
                                       getPrimitiveType(ctx->types, prtU64)));

        AstNode *assert = makeExprStmt(
            ctx->pool,
            &node->loc,
            flgNone,
            makeCallExpr(ctx->pool,
                         &node->loc,
                         makeResolvedIdentifier(ctx->pool,
                                                &node->loc,
                                                S___cxy_assert,
                                                0,
                                                builtinAssert,
                                                NULL,
                                                builtinAssert->type),
                         args.first,
                         node->flags,
                         NULL,
                         builtinAssert->type->func.retType),
            NULL,
            makeVoidType(ctx->types));
        astModifierAdd(&ctx->block, assert);
    }
}

static void visitIndexExpr(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *target = node->indexExpr.target, *index = node->indexExpr.index;
    astVisit(visitor, target);
    astVisit(visitor, index);
    if (nodeNeedsTemporaryVar(index)) {
        AstNode *var = makeVarDecl(ctx->pool,
                                   &index->loc,
                                   flgTemporary | (index->flags & flgConst),
                                   makeAnonymousVariable(ctx->strings, "_idx"),
                                   NULL,
                                   deepCloneAstNode(ctx->pool, index),
                                   NULL,
                                   index->type);
        index->tag = astIdentifier;
        index->ident.value = var->_namedNode.name;
        index->ident.resolvesTo = var;
        index->flags |= flgMove;
        astModifierAdd(&ctx->block, var);
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

        if (nodeNeedsTemporaryVar(arg)) {
            astVisit(visitor, arg);
            AstNode *var =
                makeVarDecl(ctx->pool,
                            &arg->loc,
                            flgTemporary | (arg->flags & flgConst),
                            makeAnonymousVariable(ctx->strings, "_a"),
                            NULL,
                            deepCloneAstNode(ctx->pool, arg),
                            NULL,
                            arg->type);
            arg->tag = astIdentifier;
            arg->ident.value = var->_namedNode.name;
            arg->ident.resolvesTo = var;
            arg->flags |= flgMove;
            astModifierAdd(&ctx->block, var);
        }

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

    if (isReferenceType(this->type) && isClassType(target->type)) {
        target = makeReferenceOfExpr(
            ctx->pool, &target->loc, this->flags, target, NULL, this->type);
    }
    else if (isPointerType(this->type) && !(typeIs(target->type, Pointer) ||
                                            typeIs(target->type, Reference))) {
        target = makePointerOfExpr(
            ctx->pool, &target->loc, this->flags, target, NULL, this->type);
    }

    target->flags |= flgReference;
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
    if (nodeIs(target, FuncDecl) || nodeIs(target, ExternDecl) ||
        (nodeIs(target, VarDecl) && hasFlag(target, TopLevelDecl))) //
    {
        NodeToExternDecl *f2e = n2eGetNodeToExternDecl(&ctx->n2e, target);
        if (f2e == NULL) {
            // new function declaration added, add extern
            AstNode *decl =
                nodeIs(target, ExternDecl)
                    ? deepCloneAstNode(ctx->pool, target)
                    : makeAstNode(ctx->pool,
                                  &target->loc,
                                  &(AstNode){.tag = astExternDecl,
                                             .type = target->type,
                                             .flags = target->flags,
                                             .externDecl.func = target});

            n2eAddNodeToExternDecl(&ctx->n2e, target, decl);
            astModifierAddHead(&ctx->root, decl);
            node->ident.resolvesTo = decl;
        }
        else {
            node->ident.resolvesTo = f2e->target;
        }
    }
}

static void visitBinaryExpr(AstVisitor *visitor, AstNode *node)
{
    astVisitFallbackVisitAll(visitor, node);
    if (node->binaryExpr.op == opIs) {
        SimplifyContext *ctx = getAstVisitorContext(visitor);
        AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
        const Type *type = stripPointerOrReferenceOnce(lhs->type, NULL);
        csAssert0(isUnionType(type));
        u32 idx = findUnionTypeIndex(type, rhs->type);
        csAssert0(idx != UINT32_MAX);
        // Convert to lhs.tag == idx
        lhs->memberExpr.target = deepCloneAstNode(ctx->pool, lhs);
        lhs->tag = astMemberExpr;
        lhs->memberExpr.member =
            makeIntegerLiteral(ctx->pool,
                               &lhs->loc,
                               0,
                               NULL,
                               getPrimitiveType(ctx->types, prtU64));
        lhs->type = getPrimitiveType(ctx->types, prtU64);

        rhs->tag = astIntegerLit;
        rhs->intLiteral.isNegative = false;
        rhs->intLiteral.uValue = idx;
        rhs->type = getPrimitiveType(ctx->types, prtU64);

        node->binaryExpr.op = opEq;
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
    AstNode *base = node->path.elements;
    if (hasFlag(base, AddThis)) {
        AstNode *this = getMemberFunctionThis(ctx->currentFunction);
        csAssert0(this);
        node->path.elements = makeResolvedPathElement(
            ctx->pool, &node->loc, S_this, this->flags, this, base, this->type);

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

static void visitMemberExpr(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    astVisitFallbackVisitAll(visitor, node);
    if (!nodeIs(node, MemberExpr))
        return;

    AstNode *target = node->memberExpr.target;
    if (nodeNeedsTemporaryVar(target) && !nodeIs(target, TypeRef)) {
        AstNode *var = makeVarDecl(ctx->pool,
                                   &target->loc,
                                   flgTemporary,
                                   makeAnonymousVariable(ctx->strings, "_m"),
                                   NULL,
                                   target,
                                   NULL,
                                   target->type);
        node->memberExpr.target = makeResolvedIdentifier(ctx->pool,
                                                         &target->loc,
                                                         var->_namedNode.name,
                                                         0,
                                                         var,
                                                         NULL,
                                                         target->type);
        astModifierAdd(&ctx->block, var);
    }
}

static void visitReferenceOfExpr(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    astVisitFallbackVisitAll(visitor, node);
    AstNode *operand = node->unaryExpr.operand;
    if (nodeNeedsTemporaryVar(operand)) {
        AstNode *var = makeVarDecl(ctx->pool,
                                   &operand->loc,
                                   flgTemporary,
                                   makeAnonymousVariable(ctx->strings, "_r"),
                                   NULL,
                                   operand,
                                   NULL,
                                   operand->type);
        node->unaryExpr.operand = makeResolvedIdentifier(ctx->pool,
                                                         &operand->loc,
                                                         var->_namedNode.name,
                                                         0,
                                                         var,
                                                         NULL,
                                                         operand->type);
        astModifierAdd(&ctx->block, var);
    }
}

static void visitStructExpr(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *field = node->structExpr.fields;
    for (; field; field = field->next) {
        AstNode *target =
            findMemberDeclInType(node->type, field->fieldExpr.name);
        csAssert0(target);
        field->fieldExpr.index = target->structField.index;
        const Type *left = unwrapType(target->type, NULL),
                   *right = unwrapType(field->fieldExpr.value->type, NULL);
        if (left != right || typeIs(left, Func))
            simplifyCastExpression(ctx, field->fieldExpr.value, left);
        astVisit(visitor, field->fieldExpr.value);
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
            member->structField.index = i++;
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
    AstModifier block = ctx->block;
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
            }
        }
        last = stmt;
    }

    if (last && hasFlag(node, BlockReturns))
        last->flags |= flgBlockValue;

    node->blockStmt.last = last;
    ctx->block = block;
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

void visitReturnStmt(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->returnStmt.expr;
    if (expr == NULL)
        return;
    astVisit(visitor, expr);
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

void visitSwitchStmt(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->matchStmt.expr, *esac = node->switchStmt.cases;
    astVisit(visitor, expr);
    for (; esac; esac = esac->next) {
        AstNode *body = esac->caseStmt.body;
        if (body && !nodeIs(body, BlockStmt)) {
            esac->caseStmt.body = makeBlockStmt(
                ctx->pool, &body->loc, body, NULL, makeVoidType(ctx->types));
        }
        astVisitFallbackVisitAll(visitor, esac);
    }
}

void visitMatchStmt(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->matchStmt.expr;
    astVisit(visitor, expr);
    AstNode *cond = deepCloneAstNode(ctx->pool, expr);
    expr->memberExpr.target = deepCloneAstNode(ctx->pool, expr);
    expr->tag = astMemberExpr;
    expr->memberExpr.member = makeIntegerLiteral(
        ctx->pool, &expr->loc, 0, NULL, getPrimitiveType(ctx->types, prtU64));
    expr->type = getPrimitiveType(ctx->types, prtU64);

    node->tag = astSwitchStmt;

    for (AstNode *esac = node->matchStmt.cases; esac; esac = esac->next) {
        AstNode *match = esac->caseStmt.match,
                *variable = esac->caseStmt.variable,
                *body = esac->caseStmt.body;
        if (match) {
            esac->caseStmt.match = makeIntegerLiteral(
                ctx->pool, &match->loc, esac->caseStmt.idx, NULL, expr->type);
            if (variable) {
                variable->flags |= (cond->flags & flgConst) | flgTemporary;
                variable->varDecl.init =
                    makeCastExpr(ctx->pool,
                                 &variable->loc,
                                 flgUnionCast,
                                 deepCloneAstNode(ctx->pool, cond),
                                 makeTypeReferenceNode(
                                     ctx->pool, variable->type, &variable->loc),
                                 NULL,
                                 cond->type);
                variable->varDecl.init->castExpr.idx = esac->caseStmt.idx;
                if (nodeIs(body, BlockStmt)) {
                    variable->next = body->blockStmt.stmts;
                    body->blockStmt.stmts = variable;
                }
                else {
                    variable->next = body;
                    esac->caseStmt.body =
                        makeBlockStmt(ctx->pool,
                                      &body->loc,
                                      variable,
                                      NULL,
                                      makeVoidType(ctx->types));
                }
                esac->caseStmt.variable = NULL;
            }
        }
        else if (!nodeIs(body, BlockStmt)) {
            esac->caseStmt.body = makeBlockStmt(
                ctx->pool, &body->loc, body, NULL, makeVoidType(ctx->types));
        }
        astVisit(visitor, esac);
    }
}

static void visitFuncDecl(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    ctx->currentFunction = node;
    n2eAddNodeToExternDecl(&ctx->n2e, node, node);
    if (node->funcDecl.this_) {
        node->funcDecl.signature->params = node->funcDecl.this_;
    }
    AstNode *body = node->funcDecl.body;
    if (nodeIs(body, ExprStmt)) {
        AstNode *ret = makeReturnAstNode(ctx->pool,
                                         &body->loc,
                                         flgNone,
                                         body->exprStmt.expr,
                                         NULL,
                                         body->type);
        body->tag = astBlockStmt;
        body->blockStmt.stmts = ret;
    }
    astVisitFallbackVisitAll(visitor, node);
    ctx->currentFunction = NULL;
}

static void visitVarDecl(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    n2eAddNodeToExternDecl(&ctx->n2e, node, node);
    AstNode *init = node->varDecl.init;
    if (!hasFlag(node, TopLevelDecl)) {
        astVisit(visitor, init);
        return;
    }

    if (!isStaticExpr(init)) {
        if (typeIs(init->type, Class)) {
            node->varDecl.init =
                makeNullLiteral(ctx->pool, &init->loc, NULL, init->type);
        }
        else {
            node->varDecl.init = NULL;
        }
        // just a list of assignment expressions
        insertAstNode(&ctx->init,
                      makeExprStmt(ctx->pool,
                                   &node->loc,
                                   flgNone,
                                   makeAssignExpr(ctx->pool,
                                                  &node->loc,
                                                  node->flags,
                                                  makeResolvedIdentifier(
                                                      ctx->pool,
                                                      &node->loc,
                                                      node->varDecl.name,
                                                      0,
                                                      node,
                                                      NULL,
                                                      node->type),
                                                  opAssign,
                                                  init,
                                                  NULL,
                                                  init->type),
                                   NULL,
                                   makeVoidType(ctx->types)));
    }
    else {
        astVisit(visitor, init);
    }
}

#ifdef LLVM_BACKEND
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
            65535 - i,
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
#endif

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
                               .traceMemory = driver->options.withMemoryTrace &&
                                              isBuiltinsInitialized()};
    n2eInit(&context.n2e, driver->pool);

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context, {
        [astProgram] = visitProgram,
        [astPath] = visitPathExpr,
        [astPathElem] = visitPathElement,
        [astIdentifier] = visitIdentifier,
        [astBinaryExpr] = visitBinaryExpr,
        [astIndexExpr] = visitIndexExpr,
        [astCallExpr] = visitCallExpr,
        [astCastExpr] = visitCastExpr,
        [astStmtExpr] = visitStmtExpr,
        [astStructExpr] = visitStructExpr,
        [astMemberExpr] = visitMemberExpr,
        [astReferenceOf] = visitReferenceOfExpr,
        [astPointerOf] = visitReferenceOfExpr,
        [astForStmt] = visitForStmt,
        [astBlockStmt] = visitBlockStmt,
        [astWhileStmt] = visitWhileStmt,
        [astIfStmt] = visitIfStmt,
        [astReturnStmt] = visitReturnStmt,
        [astMatchStmt] = visitMatchStmt,
        [astSwitchStmt] = visitSwitchStmt,
        [astStructDecl] = visitStructDecl,
        [astClassDecl] = visitStructDecl,
        [astFuncDecl] = visitFuncDecl,
        [astVarDecl] = visitVarDecl,
        [astGenericDecl] = astVisitSkip,
        [astMacroDecl] = astVisitSkip
    }, .fallback = astVisitFallbackVisitAll, .dispatch = withSavedStack);
    // clang-format on

    astVisit(&visitor, node);
    n2eDeinit(&context.n2e);

#ifdef LLVM_BACKEND
    if (hasFlag(node, Main) && driver->startup.first) {
        simplifyMainModule(&context, node);
    }
#endif
    return node;
}

AstNode *simplifyAst(CompilerDriver *driver, AstNode *node)
{
    node = simplifyDeferStatements(driver, node);
    if (node == NULL || hasErrors(driver->L))
        return node;

    return simplifyCode(driver, node);
}
