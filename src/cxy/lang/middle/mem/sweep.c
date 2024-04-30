//
// Created by Carter Mbotho on 2024-04-19.
//

#include "mem.h"

#include "core/alloc.h"
#include "driver/driver.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/visitor.h"

#include "lang/middle/builtins.h"

#define bscConditionalBlock BIT(1)

static inline bool isConditionalNode(MMContext *ctx, AstNode *block)
{
    switch (block->tag) {
    case astIfStmt:
    case astMatchStmt:
    case astCaseStmt:
    case astForStmt:
    case astWhileStmt:
        return true;
    default:
        return false;
    }
}

static inline bool isCopySemanticsSupported(const Type *type)
{
    return hasFlag(type, ReferenceMembers) &&
           (isStructType(type) || isTupleType(type));
}

static inline bool isSharedMemorySupported(const Type *type)
{
    return isClassType(type);
}

static inline void pushBlockScope(MMContext *ctx, AstNode *node)
{
    u64 flags = isConditionalNode(ctx, node) ? bscConditionalBlock : flgNone;
    blockScopeContainerPush(&ctx->bsc, node, flags);
}

static inline void popBlockScope(MMContext *ctx)
{
    blockScopeContainerPop(&ctx->bsc);
}

static void addVariableToScope(MMContext *ctx, AstNode *variable)
{
    BlockScope *scope = ctx->bsc.scope;
    if (scope == NULL)
        return;

    pushOnDynArray(&scope->data,
                   &(VariableTrace){.variable = variable, .scope = scope});

    insertInHashTable(&ctx->allVariables,
                      &(VariableTrace){.variable = variable, .scope = scope},
                      hashPtr(hashInit(), variable),
                      sizeof(VariableTrace),
                      comparePointers);
}

static VariableTrace *getVariableTrace(MMContext *ctx, AstNode *variable)
{
    return findInHashTable(&ctx->allVariables,
                           &(VariableTrace){.variable = variable},
                           hashPtr(hashInit(), variable),
                           sizeof(VariableTrace),
                           comparePointers);
}

static bool moveWithoutNullifying(MMContext *ctx, AstNode *node)
{
    if (!nodeIs(node, Identifier))
        return false;
    AstNode *resolved = node->ident.resolvesTo;
    if (!nodeIs(resolved, VarDecl) && !nodeIs(resolved, FuncParamDecl))
        return false;
    VariableTrace *trace = getVariableTrace(ctx, resolved);
    csAssert0(trace);

    BlockScope *scope = ctx->bsc.scope;
    while (scope != trace->scope) {
        if (scope->flags & bscConditionalBlock)
            return false;
        scope = scope->next;
    }
    resolved->flags |= flgMoved;
    return true;
}

static AstNode *findExternBuiltinDecl(MMContext *ctx, cstring name)
{
    AstNode *decl = ctx->program->program.decls;
    for (; decl; decl = decl->next) {
        if (nodeIs(decl, ExternDecl) &&
            decl->externDecl.func->funcDecl.name == name)
            return decl;
    }
    return NULL;
}

static inline AstNode *getResolvedAstNode(const AstNode *node)
{
    if (nodeIs(node, Identifier)) {
        return node->ident.resolvesTo;
    }
    if (nodeIs(node, MemberExpr))
        return getResolvedAstNode(node->memberExpr.member);
    return NULL;
}

static inline bool isMemoryManagementNeeded(const AstNode *node)
{
    if (node == NULL || hasFlag(node, Transient))
        return false;
    const Type *type = node->type;
    if (!isSharedMemorySupported(type) && !isCopySemanticsSupported(type))
        return false;

    AstNode *resolved = NULL;
    switch (node->tag) {
    case astIdentifier:
        resolved = node->ident.resolvesTo;
        break;
    case astIndexExpr:
        return true;
    case astMemberExpr: {
        AstNode *member = node->memberExpr.member;
        if (nodeIs(member, IntegerLit))
            return true;
        if (!nodeIs(member, Identifier))
            return false;
        resolved = member->ident.resolvesTo;
        break;
    }
    default:
        return false;
    }

    switch (resolved->tag) {
    case astVarDecl:
    case astFieldDecl:
    case astFuncParamDecl:
        return true;
    default:
        return false;
    }
}

static void implementSharedPointerGet(MMContext *ctx, AstNode *node)
{
    if (hasFlag(node, Move))
        return;

    AstNode *sptrGet = findExternBuiltinDecl(ctx, S_sptr_ref);
    csAssert0(sptrGet);
    AstNode *arg = shallowCloneAstNode(ctx->pool, node);
    arg->type = makeVoidPointerType(ctx->types, flgNone);

    // change this node to a call expression
    clearAstBody(node);
    node->tag = astCallExpr;
    node->callExpr.callee = makeResolvedIdentifier(
        ctx->pool, &node->loc, S_sptr_ref, 0, sptrGet, NULL, sptrGet->type);
    node->callExpr.args = arg;
}

// static AstNode *implementStructCopy(MMContext *ctx,
//                                     const Type *type,
//                                     AstNode *node)
//{
//     AstNode *decl = getTypeDecl(type), *member = decl->structDecl.members;
//     AstNodeList fields = {};
//     for (; member; member = member->next) {
//         if (!nodeIs(member, FieldDecl))
//             continue;
//         if (isClassType(member->type)) {
//         }
//         else if (isStructType(member->type)) {
//             insertAstNode(
//                 &fields,
//                 makeMemberExpr(ctx->pool,
//                                &node->loc,
//                                flgNone,
//                                deepCloneAstNode(ctx->pool, node),
//                                makeResolvedIdentifier(ctx->pool,
//                                                       &node->loc,
//                                                       member->structField.name,
//                                                       0,
//                                                       member,
//                                                       NULL,
//                                                       member->type),
//                                NULL,
//                                member->type));
//         }
//         else if (isTupleType(member->type)) {
//         }
//     }
// }
//
// static AstNode *implementTupleCopy(MMContext *ctx, AstNode *node) {}

static void implementSmartPointerDrop(MMContext *ctx, AstNode *node)
{
    AstNode *sptrDrop = findExternBuiltinDecl(ctx, S_sptr_drop);
    csAssert0(sptrDrop);
    AstNode *arg = shallowCloneAstNode(ctx->pool, node);
    arg->type = makeVoidPointerType(ctx->types, flgNone);

    // change this node to a call expression
    clearAstBody(node);

    node->tag = astCallExpr;
    node->callExpr.callee = makeResolvedIdentifier(
        ctx->pool, &node->loc, S_sptr_drop, 0, sptrDrop, NULL, sptrDrop->type);
    node->callExpr.args = arg;

    astModifierAdd(&ctx->block, node);
}

static void implementVariableCleanup(MMContext *ctx, AstNode *node)
{
    AstNode *sptrDrop = findExternBuiltinDecl(ctx, S_sptr_drop);
    csAssert0(sptrDrop);

    astModifierAdd(&ctx->block,
                   makeCallExpr(ctx->pool,
                                &node->loc,
                                makeResolvedIdentifier(ctx->pool,
                                                       &node->loc,
                                                       S_sptr_drop,
                                                       0,
                                                       sptrDrop,
                                                       NULL,
                                                       sptrDrop->type),
                                makeResolvedIdentifier(
                                    ctx->pool,
                                    &node->loc,
                                    node->_namedNode.name,
                                    0,
                                    node,
                                    NULL,
                                    makeVoidPointerType(ctx->types, flgNone)),
                                node->flags,
                                NULL,
                                makeVoidType(ctx->types)));
}

static void blockScopeSeal(MMContext *ctx, BlockScope *scope)
{
    if (scope == NULL)
        return;

    for (i64 i = (i64)scope->data.size - 1; i >= 0; i--) {
        AstNode *variable =
            dynArrayAt(VariableTrace *, &scope->data, i).variable;
        if (!hasFlags(variable, flgMoved | flgTransient)) {
            implementVariableCleanup(ctx, variable);
        }
    }
}

static void blockScopeSealAll(MMContext *ctx)
{
    BlockScope *scope = ctx->bsc.scope;
    while (scope) {
        blockScopeSeal(ctx, scope);
        scope = scope->next;
    }
}

static void visitIdentifierExpr(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    AstNode *resolved = node->ident.resolvesTo, *parent = getParentScope(node);
    if (hasFlag(node, Move) || hasFlag(parent, Transient))
        return;

    if (hasFlag(resolved, Moved)) {
        logError(ctx->L,
                 &node->loc,
                 "attempt to use a previously moved declaration",
                 NULL);
    }
}

static void visitAssignExpr(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    astVisit(visitor, node->assignExpr.lhs);
    if (isMemoryManagementNeeded(node->assignExpr.lhs)) {
        implementSmartPointerDrop(
            ctx, deepCloneAstNode(ctx->pool, node->assignExpr.lhs));
    }

    astVisit(visitor, node->assignExpr.rhs);
    if (isMemoryManagementNeeded(node->assignExpr.rhs)) {
        implementSharedPointerGet(ctx, node->assignExpr.rhs);
    }
}

static void visitCallExpr(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    AstNode *callee = node->callExpr.callee;
    astVisit(visitor, callee);
    AstNode *arg = node->callExpr.args;

    for (; arg; arg = arg->next) {
        astVisit(visitor, arg);
        // arguments passed to functions marked a pure are left alone
        if (hasFlag(callee, Pure) || hasFlag(callee, Extern))
            continue;

        if (isMemoryManagementNeeded(arg)) {
            implementSharedPointerGet(ctx, arg);
        }
    }
}

static void visitStructExpr(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    AstNode *field = node->structExpr.fields;
    for (; field; field = field->next) {
        astVisit(visitor, field->fieldExpr.value);
        if (isMemoryManagementNeeded(field->fieldExpr.value)) {
            implementSharedPointerGet(ctx, field->fieldExpr.value);
        }
    }
}

static void visitTupleExpr(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    AstNode *member = node->tupleExpr.elements;
    for (; member; member = member->next) {
        astVisit(visitor, member);
        if (isMemoryManagementNeeded(member)) {
            implementSharedPointerGet(ctx, member);
        }
    }
}

static void visitArrayExpr(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    AstNode *member = node->arrayExpr.elements;
    for (; member; member = member->next) {
        astVisit(visitor, member);
        if (isMemoryManagementNeeded(member)) {
            implementSharedPointerGet(ctx, member);
        }
    }
}

static void visitUnaryExpr(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    AstNode *operand = node->unaryExpr.operand;
    if (node->unaryExpr.op == opMove && isClassType(operand->type)) {
        operand->flags |= flgMove;
        astVisit(visitor, operand);
        if (!moveWithoutNullifying(ctx, operand)) {
            cstring name = makeAnonymousVariable(ctx->strings, "mv");
            AstNode *var = makeVarDecl(
                ctx->pool,
                &operand->loc,
                flgNone,
                name,
                makeTypeReferenceNode(ctx->pool, operand->type, &operand->loc),
                operand,
                NULL,
                operand->type);
            var->next = makeAssignExpr(
                ctx->pool,
                &operand->loc,
                flgNone,
                deepCloneAstNode(ctx->pool, operand),
                opAssign,
                makeNullLiteral(
                    ctx->pool, &operand->loc, NULL, makeNullType(ctx->types)),
                makeResolvedIdentifier(ctx->pool,
                                       &operand->loc,
                                       name,
                                       0,
                                       var,
                                       NULL,
                                       operand->type),
                operand->type);

            clearAstBody(node);
            node->tag = astStmtExpr;
            node->stmtExpr.stmt = makeBlockStmt(
                ctx->pool, &operand->loc, var, NULL, operand->type);
            node->stmtExpr.stmt->flags |= flgBlockReturns;
        }
    }
    else {
        astVisit(visitor, operand);
    }
}

static void visitVariableDecl(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    if (isClassType(node->type)) {
        addVariableToScope(ctx, node);
    }

    if (node->varDecl.init == NULL)
        return;

    astVisit(visitor, node->varDecl.init);
    if (isMemoryManagementNeeded(node->varDecl.init)) {
        implementSharedPointerGet(ctx, node->varDecl.init);
    }
}

static void visitFuncDecl(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    AstNode *param = node->funcDecl.signature->params;
    ctx->allVariables = newHashTable(sizeof(VariableTrace));
    pushBlockScope(ctx, node);
    if (!hasFlag(node, Pure)) {
        for (; param; param = param->next) {
            if (isClassType(param->type) && param->_namedNode.name != S_this) {
                addVariableToScope(ctx, param);
            }
        }
    }
    astVisit(visitor, node->funcDecl.body);
    freeHashTable(&ctx->allVariables);
    popBlockScope(ctx);
}

static void visitBlockStmt(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    AstNode *stmt = node->blockStmt.stmts, *parent = getParentScope(node);

    astModifierInit(&ctx->block, node);

    if (!nodeIs(parent, FuncDecl))
        pushBlockScope(ctx, parent);

    for (; stmt; stmt = stmt->next) {
        astModifierNext(&ctx->block, stmt);
        astVisit(visitor, stmt);
    }
    astModifierNext(&ctx->block, stmt);

    if (!node->blockStmt.sealed)
        blockScopeSeal(ctx, ctx->bsc.scope);

    if (!nodeIs(parent, FuncDecl))
        popBlockScope(ctx);
}

static void visitReturnStmt(AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->returnStmt.expr;
    if (expr && isClassType(expr->type)) {
        if (nodeIs(expr, Identifier)) {
            AstNode *resolved = expr->ident.resolvesTo;
            if (nodeIs(resolved, FuncParamDecl) || nodeIs(resolved, VarDecl)) {
                resolved->flags |= flgMoved;
            }
        }
        else {
            AstNode *tmp = makeVarDecl(
                ctx->pool,
                &expr->loc,
                flgMoved,
                makeAnonymousVariable(ctx->strings, "ret"),
                makeTypeReferenceNode(ctx->pool, expr->type, &expr->loc),
                expr,
                NULL,
                expr->type);
            astVisit(visitor, tmp);

            astModifierAdd(&ctx->block, tmp);
            node->returnStmt.expr = makeResolvedIdentifier(ctx->pool,
                                                           &expr->loc,
                                                           tmp->varDecl.name,
                                                           0,
                                                           tmp,
                                                           NULL,
                                                           tmp->type);
        }
    }
    blockScopeSealAll(ctx);
    ctx->block.parent->blockStmt.sealed = true;
}

static void withSavedStack(Visitor func, AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    __typeof(ctx->stack) stack = ctx->stack;
    node->parentScope = node->parentScope ?: ctx->current;
    ctx->current = node;
    func(visitor, node);

    ctx->stack = stack;
}

AstNode *memoryManageAst(CompilerDriver *driver, AstNode *node)
{
    MMContext context = {.L = driver->L,
                         .pool = driver->pool,
                         .strings = driver->strings,
                         .types = driver->types,
                         .program = node};

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context, {
        [astIdentifier] = visitIdentifierExpr,
        [astCallExpr] = visitCallExpr,
        [astAssignExpr] = visitAssignExpr,
        [astStructExpr] = visitStructExpr,
        [astTupleExpr] = visitTupleExpr,
        [astArrayExpr] = visitArrayExpr,
        [astUnaryExpr] = visitUnaryExpr,
        [astVarDecl] = visitVariableDecl,
        [astFuncDecl] = visitFuncDecl,
        [astBlockStmt] = visitBlockStmt,
        [astReturnStmt] = visitReturnStmt,
        [astGenericDecl] = astVisitSkip,
        [astMacroDecl] = astVisitSkip
    }, .fallback = astVisitFallbackVisitAll, .dispatch = withSavedStack);
    // clang-format on

    blockScopeContainerInit(&context.bsc, sizeof(VariableTrace));

    if (isBuiltinsInitialized())
        astVisit(&visitor, node);

    blockScopeContainerDeinit(&context.bsc);

    return node;
}