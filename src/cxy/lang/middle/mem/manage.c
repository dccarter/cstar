//
// Created by Carter Mbotho on 2024-12-09.
//
#include "mem.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/visitor.h"

#include <lang/frontend/strings.h>
#include <lang/frontend/ttable.h>

typedef struct {
    AstNode *variable;
    VariableState state;
    AstNode *dropFlags;
    AstNode *scope;
    AstNode *lastDropScope;
    bool used;
    bool destroyed;
} VariableTrace;

typedef struct {
    DynArray *locals;
    bool isLoopStart;
} TraceScope;

typedef struct {
    u64 sealDepth;
    AstNode *bb;
} CleanupBlock;

// clang-format off
#define MEM_CONTEXT_STACK               \
AstModifier blockModifier;              \
AstNode *current;                       \
AstNode *currentBlock;                  \
bool returnStmt;                        \
bool inLoop;

// clang-format on

typedef struct MemContext {
    Log *L;
    MemPool *pool;
    StrPool *strings;
    TypeTable *types;
    Hmap variables;
    DynArray scopes;
    DynArray *locals;
    DynArray *cleanupBlocks;
    AstNode *returnVariable;
    AstNode *currentFunction;
    u64 basicBlockIndex;
    union {
        struct {
            MEM_CONTEXT_STACK
        };
        struct {
            MEM_CONTEXT_STACK
        } stack;
    };
} MemContext;

static bool isLocalVariable(const AstNode *node)
{
    if (nodeIs(node, FuncParamDecl) || nodeIs(node, VarDecl))
        return !hasFlags(node, flgTopLevelDecl | flgTemporary);
    return false;
}

static bool isLeftValueExpr(const AstNode *node)
{
    if (nodeIs(node, MemberExpr))
        return isLeftValueExpr(node->memberExpr.target);
    return nodeIsLeftValue(node);
}

static bool compareVariableTrace(const void *lhs, const void *rhs)
{
    return ((VariableTrace *)lhs)->variable == ((VariableTrace *)rhs)->variable;
}

static HashCode hashVariableTrace(const void *data)
{
    const VariableTrace *vt = (VariableTrace *)data;
    return hashPtr(hashInit(), vt->variable);
}

static VariableTrace *findVariableTrace(MemContext *ctx, AstNode *node)
{
    return hmapGet(&ctx->variables, &(VariableTrace){.variable = node});
}

static void insertVariableTrace(MemContext *ctx,
                                AstNode *node,
                                VariableState state)
{
    const HmapStatus status = hmapPut(
        &ctx->variables,
        &(VariableTrace){
            .variable = node, .scope = ctx->currentBlock, .state = state});
    csAssert0(status.s);
}

static DynArray captureVariableState(MemContext *ctx)
{
    DynArray vars = newDynArray(sizeof(VariableTrace));
    // dynArrayFor(var, AstNode *, ctx->locals)
    // {
    //     VariableTrace *vt = findVariableTrace(ctx, *var);
    //     csAssert0(vt);
    //     pushOnDynArray(&vars, vt);
    // }
    HmapIterator it = hmapIterator(&ctx->variables);
    VariableTrace *vt = NULL;
    while ((vt = hmapNext(&it))) {
        pushOnDynArray(&vars, vt);
    }
    return vars;
}

static void restoreVariableState(MemContext *ctx, DynArray *vars)
{
    dynArrayFor(var, VariableTrace, vars)
    {
        VariableTrace *vt = findVariableTrace(ctx, var->variable);
        csAssert0(vt);
        const VariableState prev = vt->state;
        vt->state = var->state;
        var->state = prev;
    }
}

static bool mergeVariableStates(DynArray *lhs, const DynArray *rhs)
{
    bool allMaybeAssigned = true;
    for (u32 i = 0; i < lhs->size; i++) {
        VariableTrace *varLhs = &dynArrayAt(VariableTrace *, lhs, i);
        const VariableTrace *varRhs = &dynArrayAt(VariableTrace *, rhs, i);
        csAssert0(varLhs->variable == varRhs->variable);
        VariableState state;
        if (varLhs->state == varRhs->state)
            state = varLhs->state;
        else if (varLhs->state == vtsAssigned || varRhs->state == vtsAssigned)
            state = vtsMaybeAssigned;
        else
            state = vtsUninitialized;

        varLhs->state = state;
        allMaybeAssigned = allMaybeAssigned && state == vtsMaybeAssigned;
    }
    return allMaybeAssigned;
}

static bool nodeNeedsMemMgmt(const AstNode *node)
{
    const Type *type = resolveUnThisUnwrapType(node->type);
    if (hasFlags(node,
                 flgMove | flgTemporary | flgReference | flgTopLevelDecl) ||
        hasFlag(type, Extern)) {
        return false;
    }
    return isClassType(type) || isDestructible(type);
}

static bool vtIsUpdatedInChildScopeLoop(MemContext *ctx,
                                        const VariableTrace *vt)
{
    return ctx->inLoop && vt->scope != ctx->currentBlock &&
           vt->lastDropScope != ctx->currentBlock;
}

static bool vtNeedsMemMgmt(MemContext *ctx, const VariableTrace *vt)
{
    if (vt->state == vtsMaybeAssigned || vt->state == vtsAssigned)
        return nodeNeedsMemMgmt(vt->variable);
    return vtIsUpdatedInChildScopeLoop(ctx, vt) &&
           nodeNeedsMemMgmt(vt->variable);
}

static bool vtNeedsDropFlags(const VariableTrace *vt)
{
    AstNode *var = vt->variable;
    if (vt->state == vtsMaybeAssigned && vt->dropFlags == NULL)
        return nodeNeedsMemMgmt(var);
    return false;
}

static void insertedDropFlags(MemContext *ctx, VariableTrace *vt)
{
    AstNode *var = vt->variable;
    bool drop = nodeIs(var, VarDecl) && var->varDecl.init != NULL;
    vt->dropFlags = makeVarDecl(
        ctx->pool,
        builtinLoc(),
        flgTemporary,
        makeStringConcat(ctx->strings, "__", vt->variable->_name, "_dflags"),
        NULL,
        makeBoolLiteral(ctx->pool,
                        builtinLoc(),
                        drop,
                        NULL,
                        getPrimitiveType(ctx->types, prtBool)),
        NULL,
        getPrimitiveType(ctx->types, prtBool));
    if (nodeIs(var, VarDecl)) {
        vt->dropFlags->next = var->next;
        var->next = vt->dropFlags;
    }
    else {
        AstNode *body = ctx->currentFunction->funcDecl.body;
        csAssert0(nodeIs(body, BlockStmt));
        vt->dropFlags->next = body->blockStmt.stmts;
        body->blockStmt.stmts = vt->dropFlags;
    }
    vt->variable->varDecl.dropFlags = vt->dropFlags;
}

static void mergeUpdateVariableStates(MemContext *ctx,
                                      DynArray *lhs,
                                      const DynArray *rhs)
{
    for (u32 i = 0; i < lhs->size; i++) {
        VariableTrace *varLhs = &dynArrayAt(VariableTrace *, lhs, i);
        const VariableTrace *varRhs = &dynArrayAt(VariableTrace *, rhs, i);
        csAssert0(varLhs->variable == varRhs->variable);
        VariableState state;
        if (varLhs->state == varRhs->state)
            state = varLhs->state;
        else if (varLhs->state == vtsAssigned || varRhs->state == vtsAssigned)
            state = vtsMaybeAssigned;
        else
            state = vtsUninitialized;

        VariableTrace *vt = findVariableTrace(ctx, varLhs->variable);
        csAssert0(vt);
        vt->state = state;
        varLhs->state = state;
        if (vtNeedsDropFlags(vt))
            insertedDropFlags(ctx, vt);
    }
}

static AstNode *makeDropVariable(MemContext *ctx,
                                 VariableTrace *vt,
                                 bool updateInChildLoop)
{
    AstNode *var = vt->variable;
    AstNode *drop = makeBackendCallExpr(
        ctx->pool,
        &var->loc,
        flgNone,
        bfiDrop,
        makeResolvedIdentifier(
            ctx->pool, &var->loc, var->_name, 0, var, NULL, var->type),
        var->type);
    drop->backendCallExpr.state =
        updateInChildLoop ? vtsMaybeAssigned : vt->state;
    vt->lastDropScope = ctx->currentBlock;
    return makeExprStmt(
        ctx->pool, &var->loc, flgNone, drop, NULL, makeVoidType(ctx->types));
}

static void transformToDropVariable(MemContext *ctx,
                                    VariableTrace *vt,
                                    AstNode *node,
                                    bool updateInChildLoop)
{
    AstNode *var = vt->variable;
    AstNode *drop = makeBackendCallExpr(
        ctx->pool,
        &var->loc,
        flgNone,
        bfiDrop,
        makeResolvedIdentifier(
            ctx->pool, &var->loc, var->_name, 0, var, NULL, var->type),
        var->type);
    drop->backendCallExpr.state =
        updateInChildLoop ? vtsMaybeAssigned : vt->state;
    node->tag = astExprStmt;
    node->type = var->type;
    node->exprStmt.expr = drop;
}

static AstNode *makeDropExpr(MemContext *ctx, AstNode *expr)
{
    return makeExprStmt(
        ctx->pool,
        &expr->loc,
        flgNone,
        makeBackendCallExpr(
            ctx->pool, &expr->loc, flgNone, bfiDrop, expr, expr->type),
        NULL,
        makeVoidType(ctx->types));
}

static void transformToDropExpr(MemContext *ctx, AstNode *node, AstNode *expr)
{
    node->tag = astBackendCall;
    node->backendCallExpr.func = bfiDrop;
    node->backendCallExpr.args = expr;
}

static AstNode *makeCopyExpr(MemContext *ctx, AstNode *expr)
{
    return makeBackendCallExpr(
        ctx->pool, &expr->loc, flgNone, bfiCopy, expr, expr->type);
}

static void transformToCopyExpr(MemContext *ctx, AstNode *node, AstNode *expr)
{
    node->tag = astBackendCall;
    node->backendCallExpr.func = bfiCopy;
    node->backendCallExpr.args = expr;
}

static void sealCurrentJumpPoint(MemContext *ctx, bool isReturn)
{
    if (dynArrayEmpty(&ctx->scopes))
        return;
    for (int i = (int)ctx->scopes.size - 1; i >= 0; i--) {
        TraceScope *scope = &dynArrayAt(TraceScope *, &ctx->scopes, i);
        dynArrayFor(var, AstNode *, scope->locals)
        {
            VariableTrace *vt = findVariableTrace(ctx, var[0]);
            csAssert0(vt);
            if (vtNeedsMemMgmt(ctx, vt)) {
                astModifierAdd(&ctx->blockModifier,
                               makeDropVariable(ctx, vt, false));
            }
        }
        if (!isReturn && scope->isLoopStart)
            break;
    }
    ctx->currentBlock->blockStmt.sealed = true;
}

static void sealAndFinalizeCurrentBlock(MemContext *ctx)
{
    dynArrayFor(var, AstNode *, ctx->locals)
    {
        VariableTrace *vt = findVariableTrace(ctx, var[0]);
        csAssert0(vt);
        if (vtNeedsMemMgmt(ctx, vt)) {
            astModifierAddAsNext(&ctx->blockModifier,
                                 makeDropVariable(ctx, vt, false));
        }
    }
}

static void updateVariableStates(MemContext *ctx, const DynArray *vars)
{
    dynArrayFor(var, VariableTrace, vars)
    {
        VariableTrace *vt = findVariableTrace(ctx, var->variable);
        csAssert0(vt);
        vt->state = var->state;
    }
}

static void reportUseUnassigned(MemContext *ctx,
                                const AstNode *node,
                                const VariableTrace *vt)
{
    const AstNode *var = vt->variable;
    if (vt->state == vtsAssigned)
        return;

    static const char *msg[] = {
        [vtsUninitialized] = "an uninitialized",
        [vtsAssigned] = "assigned",
        [vtsMaybeAssigned] = "maybe-assigned",
        [vtsMoved] = "a moved",
        [vtsDropped] = "a deleted",
    };

    if (vt->state != vtsMaybeAssigned) {
        logError(ctx->L,
                 &node->loc,
                 "use of {s} variable `{s}`",
                 (FormatArg[]){{.s = msg[vt->state]}, {.s = node->_name}});
    }
    else {
        logWarningWithId(
            ctx->L,
            wrnMaybeUninitialized,
            &node->loc,
            "use of variable '{s}' which might uninitialized or moved "
            "within a branch ",
            (FormatArg[]){{.s = node->_name}});
    }
}

static bool isVariableUnused(const VariableTrace *vt, cstring name)
{
    return !vt->used && name != S_this &&
           findAttribute(vt->variable, S_unused) == NULL;
}

static void visitVariableDecl(AstVisitor *visitor, AstNode *node)
{
    if (hasFlags(node, flgTopLevelDecl | flgComptime))
        return;
    MemContext *ctx = getAstVisitorContext(visitor);
    AstNode *init = node->varDecl.init;
    if (!isLocalVariable(node)) {
        astVisit(visitor, init);
        return;
    }

    insertVariableTrace(ctx, node, init ? vtsAssigned : vtsUninitialized);
    pushOnDynArray(ctx->locals, &node);
    astVisit(visitor, init);
    if (isLeftValueExpr(init) && nodeNeedsMemMgmt(init))
        node->varDecl.init = makeCopyExpr(ctx, init);
}

static void visitIdentifier(AstVisitor *visitor, AstNode *node)
{
    MemContext *ctx = getAstVisitorContext(visitor);
    AstNode *resolved = resolveIdentifier(node);

    if (isLocalVariable(resolved)) {
        VariableTrace *vt = findVariableTrace(ctx, resolved);
        csAssert0(vt);
        vt->used = true;
        reportUseUnassigned(ctx, node, vt);
    }
}

static void visitUnaryExpr(AstVisitor *visitor, AstNode *node)
{
    MemContext *ctx = getAstVisitorContext(visitor);
    astVisitFallbackVisitAll(visitor, node);
    AstNode *operand = node->unaryExpr.operand;
    AstNode *variable = resolveIdentifier(operand);
    if (isLocalVariable(variable)) {
        VariableTrace *vt = findVariableTrace(ctx, variable);
        csAssert0(vt);
        switch (node->unaryExpr.op) {
        case opMove:
            vt->state = vtsMoved;
            if (vt->destroyed && vtNeedsDropFlags(vt)) {
                insertedDropFlags(ctx, vt);
            }
            return;
        case opDelete:
            vt->state = vtsDropped;
            if (nodeNeedsMemMgmt(node)) {
                transformToDropExpr(ctx, node, operand);
            }
            return;
        default:
            break;
        }
    }

    if (node->unaryExpr.op == opDelete) {
        if (nodeNeedsMemMgmt(operand)) {
            transformToDropExpr(ctx, node, operand);
        }
        else {
            node->tag = astBackendCall;
            node->backendCallExpr.func = bfiZeromem;
            node->backendCallExpr.args = operand;
        }
    }
}

static void visitAssignExpr(AstVisitor *visitor, AstNode *node)
{
    MemContext *ctx = getAstVisitorContext(visitor);
    AstNode *lhs = resolveIdentifier(node->assignExpr.lhs);

    if (isLocalVariable(lhs)) {
        AstNode *rhs = node->assignExpr.rhs;
        VariableTrace *vt = findVariableTrace(ctx, lhs);
        astVisit(visitor, rhs);
        csAssert0(vt);
        if (vtNeedsMemMgmt(ctx, vt)) {
            astModifierAdd(&ctx->blockModifier,
                           makeDropVariable(
                               ctx, vt, vtIsUpdatedInChildScopeLoop(ctx, vt)));
        }
        if (nodeIs(rhs, NullLit)) {
            node->tag = astNoop;
            vt->state = isClassType(lhs->type) ? vtsDropped : vtsAssigned;
            return;
        }
        vt->state = vtsAssigned;
        return;
    }

    lhs = node->assignExpr.lhs;
    if (isLeftValueExpr(lhs) && nodeNeedsMemMgmt(lhs)) {
        astModifierAdd(&ctx->blockModifier,
                       makeDropExpr(ctx, deepCloneAstNode(ctx->pool, lhs)));
        astVisit(visitor, node->assignExpr.rhs);
    }
    else {
        astVisitFallbackVisitAll(visitor, node);
    }

    AstNode *rhs = node->assignExpr.rhs;
    if (isLeftValueExpr(rhs) && nodeNeedsMemMgmt(rhs)) {
        node->assignExpr.rhs = makeCopyExpr(ctx, rhs);
    }
}

static void visitCallExpr(AstVisitor *visitor, AstNode *node)
{
    MemContext *ctx = getAstVisitorContext(visitor);
    AstNode *callee = node->callExpr.callee;
    AstNode *arg = node->callExpr.args;
    const Type *func = callee->type;
    const AstNode *decl = getTypeDecl(func);

    astVisit(visitor, callee);
    if (hasFlag(decl, Extern)) {
        astVisitManyNodes(visitor, arg);
        return;
    }

    AstNode *param = nodeGetFuncParams(decl);
    if (nodeIsThisParam(param)) {
        param = param->next;
        astVisit(visitor, arg);
        arg = arg ? arg->next : NULL;
    }

    for (; arg; arg = arg->next, param = param->next) {
        astVisit(visitor, arg);
        if (isLeftValueExpr(arg) && nodeNeedsMemMgmt(param) &&
            !hasFlag(arg, Move))
            transformToCopyExpr(ctx, arg, deepCloneAstNode(ctx->pool, arg));
    }
}

static void visitTupleExpr(AstVisitor *visitor, AstNode *node)
{
    MemContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->tupleExpr.elements;
    for (; expr; expr = expr->next) {
        astVisit(visitor, expr);
        if (isLeftValueExpr(expr) && nodeNeedsMemMgmt(expr))
            transformToCopyExpr(ctx, expr, deepCloneAstNode(ctx->pool, expr));
    }
}

static void visitStructExpr(AstVisitor *visitor, AstNode *node)
{
    MemContext *ctx = getAstVisitorContext(visitor);
    AstNode *field = node->structExpr.fields;
    for (; field; field = field->next) {
        AstNode *value = field->fieldExpr.value;
        astVisit(visitor, value);
        if (isLeftValueExpr(value) && nodeNeedsMemMgmt(value))
            field->fieldExpr.value = makeCopyExpr(ctx, value);
    }
}

static void visitArrayExpr(AstVisitor *visitor, AstNode *node)
{
    MemContext *ctx = getAstVisitorContext(visitor);
    AstNode *elem = node->arrayExpr.elements;
    for (; elem; elem = elem->next) {
        astVisit(visitor, elem);
        if (isLeftValueExpr(elem) && nodeNeedsMemMgmt(elem))
            transformToCopyExpr(ctx, elem, deepCloneAstNode(ctx->pool, elem));
    }
}

static void visitUnionValueExpr(AstVisitor *visitor, AstNode *node)
{
    MemContext *ctx = getAstVisitorContext(visitor);
    AstNode *value = node->unionValue.value;
    astVisit(visitor, value);
    if (isLeftValueExpr(value) && nodeNeedsMemMgmt(value))
        node->unionValue.value = makeCopyExpr(ctx, value);
}

static void visitReturnStmt(AstVisitor *visitor, AstNode *node)
{
    MemContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->returnStmt.expr;

    if (expr) {
        ctx->returnStmt = true;
        AstNode *var = resolveIdentifier(expr);
        astVisit(visitor, expr);

        if (isLocalVariable(var) && nodeNeedsMemMgmt(var)) {
            VariableTrace *vt = findVariableTrace(ctx, var);
            csAssert0(vt);
            vt->state = vtsMoved;
        }
        else if (isLeftValueExpr(expr) && nodeNeedsMemMgmt(expr)) {
            expr = makeCopyExpr(ctx, expr);
        }

        if (!isLiteralExpr(expr)) {
            AstNode *var = ctx->returnVariable;
            AstNode *assign =
                makeExprStmt(ctx->pool,
                             builtinLoc(),
                             flgNone,
                             makeAssignExpr(ctx->pool,
                                            &node->loc,
                                            flgNone,
                                            makeResolvedIdentifier(ctx->pool,
                                                                   &node->loc,
                                                                   var->_name,
                                                                   0,
                                                                   var,
                                                                   NULL,
                                                                   var->type),
                                            opAssign,
                                            expr,
                                            NULL,
                                            node->type),
                             NULL,
                             var->type);
            astModifierAdd(&ctx->blockModifier, assign);
            node->returnStmt.expr = makeResolvedIdentifier(
                ctx->pool, builtinLoc(), var->_name, 0, var, NULL, var->type);
        }
        ctx->returnStmt = false;
    }

    sealCurrentJumpPoint(ctx, true);
}

static void visitBreakStmt(AstVisitor *visitor, AstNode *node)
{
    MemContext *ctx = getAstVisitorContext(visitor);
    sealCurrentJumpPoint(ctx, false);
}

static void visitContinueStmt(AstVisitor *visitor, AstNode *node)
{
    MemContext *ctx = getAstVisitorContext(visitor);
    sealCurrentJumpPoint(ctx, false);
}

static void visitBlockStmt(AstVisitor *visitor, AstNode *node)
{
    MemContext *ctx = getAstVisitorContext(visitor);
    if (ctx->currentFunction == NULL)
        return;

    if (hasFlag(node, Scoping)) {
        astVisitFallbackVisitAll(visitor, node);
        return;
    }
    DynArray locals = newDynArray(sizeof(AstNode *));
    DynArray cleanups = newDynArray(sizeof(CleanupBlock));
    DynArray *prevLocals = ctx->locals;
    DynArray *prevCleanups = ctx->cleanupBlocks;
    AstNode *parent = node->parentScope;
    bool isLoop = nodeIs(node->parentScope, WhileStmt) ||
                  nodeIs(node->parentScope, ForStmt);
    ctx->locals = &locals;
    pushOnDynArray(&ctx->scopes,
                   &(TraceScope){.locals = &locals, .isLoopStart = isLoop});
    ctx->cleanupBlocks = &cleanups;

    if (nodeIs(parent, FuncDecl)) {
        AstNode *param = parent->funcDecl.signature->params;
        for (; param; param = param->next) {
            pushOnDynArray(&locals, &param);
            insertVariableTrace(ctx, param, true);
        }
    }

    ctx->currentBlock = node;
    astModifierInit(&ctx->blockModifier, node);
    for (AstNode *stmt = node->blockStmt.stmts; stmt; stmt = stmt->next) {
        astModifierNext(&ctx->blockModifier, stmt);
        if (nodeIs(stmt, BlockStmt)) {
            // This is probably just a scoping block, so capture and merge
            // update local variables to reflect changes
            DynArray outer = captureVariableState(ctx);
            DynArray child = captureVariableState(ctx);
            astVisit(visitor, stmt);
            restoreVariableState(ctx, &child);
            mergeUpdateVariableStates(ctx, &child, &child);
            freeDynArray(&outer);
            freeDynArray(&child);
        }
        else {
            astVisit(visitor, stmt);
        }
    }

    if (nodeIs(parent, WhileStmt) && parent->whileStmt.update) {
        // Visit update within this blocks context
        astVisit(visitor, parent->whileStmt.update);
    }

    // Block cleanup
    if (!node->blockStmt.sealed) {
        sealAndFinalizeCurrentBlock(ctx);
    }

    popDynArray(&ctx->scopes);

    dynArrayFor(var, AstNode *, &locals)
    {
        VariableTrace *vt = findVariableTrace(ctx, *var);
        csAssert0(vt);
        cstring name = getDeclarationName(vt->variable);
        if (isVariableUnused(vt, name)) {
            logWarningWithId(ctx->L,
                             wrnUnusedVariable,
                             &vt->variable->loc,
                             "variable '{s}' declared but not used",
                             (FormatArg[]){{.s = name}});
        }

        hmapRemove(&ctx->variables, vt);
    }

    ctx->locals = prevLocals;
    ctx->cleanupBlocks = prevCleanups;
    freeDynArray(&locals);
}

static void visitIfStmt(AstVisitor *visitor, AstNode *node)
{
    MemContext *ctx = getAstVisitorContext(visitor);
    astVisit(visitor, node->ifStmt.cond);
    DynArray then = captureVariableState(ctx);
    DynArray esle = captureVariableState(ctx);
    if (node->ifStmt.body) {
        astVisit(visitor, node->ifStmt.body);
        restoreVariableState(ctx, &then);
    }
    if (node->ifStmt.otherwise) {
        astVisit(visitor, node->ifStmt.otherwise);
        restoreVariableState(ctx, &esle);
    }
    mergeUpdateVariableStates(ctx, &then, &esle);
    freeDynArray(&then);
    freeDynArray(&esle);
}

static void visitWhileStmt(AstVisitor *visitor, AstNode *node)
{
    MemContext *ctx = getAstVisitorContext(visitor);
    astVisit(visitor, node->whileStmt.cond);

    DynArray lhs = captureVariableState(ctx);
    DynArray rhs = captureVariableState(ctx);
    ctx->inLoop = true;
    astVisit(visitor, node->whileStmt.body);
    restoreVariableState(ctx, &lhs);
    restoreVariableState(ctx, &rhs);
    mergeUpdateVariableStates(ctx, &lhs, &rhs);
    freeDynArray(&lhs);
    freeDynArray(&rhs);
}

static void visitSwitch(AstVisitor *visitor, AstNode *node)
{
    MemContext *ctx = getAstVisitorContext(visitor);
    astVisit(visitor, node->switchStmt.cond);
    DynArray lhs = captureVariableState(ctx);
    for (AstNode *esac = node->switchStmt.cases; esac; esac = esac->next) {
        DynArray rhs = captureVariableState(ctx);
        astVisit(visitor, esac);
        restoreVariableState(ctx, &rhs);
        mergeVariableStates(&lhs, &rhs);
        freeDynArray(&rhs);
    }
    updateVariableStates(ctx, &lhs);
    freeDynArray(&lhs);
}

static void visitFuncDecl(AstVisitor *visitor, AstNode *node)
{
    MemContext *ctx = getAstVisitorContext(visitor);
    AstNode *body = node->funcDecl.body;
    if (body == NULL)
        return;

    const Type *type = node->type;
    if (!typeIs(type->func.retType, Void)) {
        AstNode *returnVar =
            makeVarDecl(ctx->pool,
                        &body->loc,
                        flgReturned | flgTemporary,
                        makeAnonymousVariable(ctx->strings, "_rt"),
                        NULL,
                        NULL,
                        body->blockStmt.stmts,
                        type->func.retType);
        body->blockStmt.stmts = returnVar;
        ctx->returnVariable = returnVar;
    }

    ctx->variables = hmapCreate(
        sizeof(VariableTrace), hashVariableTrace, compareVariableTrace);
    ctx->scopes = newDynArray(sizeof(TraceScope));
    ctx->currentFunction = node;
    body->parentScope = node;

    astVisit(visitor, body);

    ctx->returnVariable = NULL;
    ctx->currentFunction = NULL;
    hmapDestroy(&ctx->variables);
    freeDynArray(&ctx->scopes);
}

static void withSavedStack(Visitor func, AstVisitor *visitor, AstNode *node)
{
    MemContext *ctx = getAstVisitorContext(visitor);
    __typeof(ctx->stack) stack = ctx->stack;
    node->parentScope = node->parentScope ?: ctx->current;
    ctx->current = node;
    func(visitor, node);

    ctx->stack = stack;
}

void manageMemory(CompilerDriver *cc, AstNode *node)
{
    MemContext context = {.L = cc->L,
                          .pool = cc->pool,
                          .strings = cc->strings,
                          .types = cc->types};
    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context, {
        [astBlockStmt] = visitBlockStmt,
        [astReturnStmt] = visitReturnStmt,
        [astContinueStmt] = visitContinueStmt,
        [astBreakStmt] = visitBreakStmt,
        [astIfStmt] = visitIfStmt,
        [astWhileStmt] = visitWhileStmt,
        [astSwitchStmt] = visitSwitch,
        [astIdentifier] = visitIdentifier,
        [astUnaryExpr] = visitUnaryExpr,
        [astAssignExpr] = visitAssignExpr,
        [astCallExpr] = visitCallExpr,
        [astTupleExpr] = visitTupleExpr,
        [astStructExpr] = visitStructExpr,
        [astArrayExpr] = visitArrayExpr,
        [astUnionValueExpr] = visitUnionValueExpr,
        [astVarDecl] = visitVariableDecl,
        [astFuncDecl] = visitFuncDecl,
        [astGenericDecl] = astVisitSkip,
        [astMacroDecl] = astVisitSkip
    }, .fallback = astVisitFallbackVisitAll, .dispatch = withSavedStack);
    // clang-format on

    astVisit(&visitor, node);
}
