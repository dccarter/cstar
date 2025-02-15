//
// Created by Carter Mbotho on 2023-07-14.
//

#include "check.h"
#include "../builtins.h"
#include "../eval/eval.h"

#include "lang/operations.h"

#include "lang/frontend/ast.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/types.h"
#include "lang/frontend/visitor.h"

#include "core/alloc.h"

#include <string.h>

static AstNode *makeSpreadVariable(TypingContext *ctx, AstNode *expr)
{
    if (nodeIs(expr, Identifier) ||
        (nodeIs(expr, Path) && expr->path.elements->next == NULL))
        return expr;

    // Create variable for this
    return makeVarDecl(ctx->pool,
                       &expr->loc,
                       expr->flags,
                       makeAnonymousVariable(ctx->strings, "_gi"),
                       NULL,
                       expr,
                       NULL,
                       expr->type);
}

static AstNode *makeVoidReturnValue(TypingContext *ctx, const Type *type)
{
    const Type *voidType = getResultTargetType(type);
    AstNode *value =
        makeStructExpr(ctx->pool,
                       builtinLoc(),
                       flgNone,
                       makeTypeReferenceNode(ctx->pool, voidType, builtinLoc()),
                       NULL,
                       NULL,
                       voidType);
    return makeUnionValueExpr(
        ctx->pool, builtinLoc(), flgNone, value, 0, NULL, type);
}

static void reportUnreachable(TypingContext *ctx, AstNode *node)
{
    AstNode *tmp = node->next;
    node->next = NULL;
    csAssert0(tmp);
    logWarning(ctx->L,
               locExtend(&tmp->loc, &getLastAstNode(tmp)->loc),
               "ignoring unreachable code",
               NULL);
}

static inline bool hasUnreachable(TypingContext *ctx, AstNode *node)
{
    return ctx->returnState && node->next &&
           (nodeIs(node, ReturnStmt) || nodeIs(node, BlockStmt) ||
            nodeIs(node, ExprStmt));
}

static const Type *getNodeReturnType(AstNode *node)
{
    if (nodeIs(node, FuncDecl) && node->type)
        return node->type->func.retType;

    if (nodeIs(node, ClosureExpr) && node->closureExpr.ret)
        return node->closureExpr.ret->type;
    return NULL;
}

static void checkTypeRef(AstVisitor *visitor, AstNode *node)
{
    attr(unused) TypingContext *ctx = getAstVisitorContext(visitor);
    csAssert0(node->type);
}

static void checkDefine(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *type = node->define.type, *container = node->define.container,
            *name = node->define.names;
    const Type *type_ = checkType(visitor, type);
    u64 count = 0;

    for (; name; name = name->next, count++) {
        name->type = type_;
    }

    node->type = type_;
    if (container == NULL || typeIs(type_, Error))
        return;

    cstring *names = mallocOrDie(sizeof(cstring) * count);
    name = node->define.names;
    for (u64 i = 0; name; name = name->next) {
        names[i] = name->ident.alias ?: name->ident.value;
    }
    qsort(names, count, sizeof(cstring), compareStrings);

    node->type = makeContainerType(
        ctx->types, container->ident.value, type_, names, count);

    free(names);
}

static void checkIdentifier(attr(unused) AstVisitor *visitor, AstNode *node)
{
    csAssert0(node->ident.resolvesTo);
    node->type = node->ident.resolvesTo->type;
}

static void checkBlockStmt(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *stmts = node->blockStmt.stmts, *stmt = stmts;
    __typeof(ctx->blockModifier) block = ctx->blockModifier;
    astModifierInit(&ctx->blockModifier, node);
    for (; stmt; stmt = stmt->next) {
        astModifierNext(&ctx->blockModifier, stmt);
        const Type *type = checkType(visitor, stmt);
        if (typeIs(type, Error)) {
            node->type = ERROR_TYPE(ctx);
            goto restoreBlock;
        }

        if (nodeIs(stmt, ReturnStmt)) {
            bool isIncompatible = node->type && !typeIs(node->type, Void) &&
                                  ctx->catcher.block == NULL &&
                                  !isTypeAssignableFrom(node->type, type);
            if (isIncompatible) {
                logError(ctx->L,
                         &stmt->loc,
                         "inconsistent return types within "
                         "block, got '{t}', "
                         "expecting '{t}'",
                         (FormatArg[]){{.t = type}, {.t = node->type}});
                node->type = ERROR_TYPE(ctx);
                goto restoreBlock;
            }

            if (!node->blockStmt.returned && node->next) {
                logWarning(
                    ctx->L,
                    manyNodesLoc(node->next),
                    "any code declared after return statement is unreachable",
                    NULL);
            }
            node->blockStmt.returned = true;
        }

        if (hasFlag(node, BlockReturns))
            node->type = type;

        if (hasUnreachable(ctx, stmt))
            break;
    }

    if (stmt && ctx->returnState)
        reportUnreachable(ctx, stmt);

    if (node->type == NULL)
        node->type = makeVoidType(ctx->types);

    AstNode *parent = node->parentScope;
    const Type *type = getNodeReturnType(parent);
    if (type && isVoidResultType(type) && !ctx->returnState) {
        AstNode *ret = makeReturnAstNode(ctx->pool,
                                         builtinLoc(),
                                         flgNone,
                                         makeVoidReturnValue(ctx, type),
                                         NULL,
                                         type);
        if (node->blockStmt.stmts) {
            getLastAstNode(node->blockStmt.stmts)->next = ret;
        }
        else {
            node->blockStmt.stmts = ret;
        }
        ret->returnStmt.isRaise = true;
        ctx->returnState = false;
    }
restoreBlock:
    ctx->blockModifier = block;
}

static void checkReturnStmt(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->returnStmt.expr, *func = node->returnStmt.func,
            *ret = nodeIs(func, FuncDecl) ? func->funcDecl.signature->ret
                                          : func->closureExpr.ret;

    const Type *expr_ =
        expr ? checkType(visitor, expr) : makeVoidType(ctx->types);
    ctx->returnState = ctx->returnState || !node->returnStmt.isRaise;
    if (typeIs(expr_, Error)) {
        node->type = expr_;
        return;
    }

    if (!exceptionVerifyRaiseExpr(ctx, ret, node))
        return;

    if (ret) {
        const Type *retType = resolveType(ret->type);
        if (expr == NULL && isVoidResultType(retType)) {
            expr_ = getResultTargetType(retType);
            expr = makeStructExpr(
                ctx->pool,
                &node->loc,
                flgNone,
                makeTypeReferenceNode(ctx->pool, expr_, &node->loc),
                NULL,
                NULL,
                expr_);
            node->returnStmt.expr = expr;
        }

        if (!isTypeAssignableFrom(ret->type, expr_)) {
            logError(ctx->L,
                     &node->loc,
                     "inconsistent return type, "
                     "expecting '{t}', got '{t}'",
                     (FormatArg[]){{.t = ret->type}, {.t = expr_}});
            logNote(ctx->L,
                    &ret->loc,
                    "return type first declared or "
                    "deduced here",
                    NULL);

            node->type = ERROR_TYPE(ctx);
            return;
        }

        node->type = retType;

        if (expr && isUnionType(retType) && retType != expr_) {
            u32 idx = findUnionTypeIndex(retType, expr_);
            csAssert0(idx != UINT32_MAX);
            node->returnStmt.expr = makeUnionValueExpr(
                ctx->pool, &expr->loc, expr->flags, expr, idx, NULL, retType);
        }

        if (!hasFlag(retType, Optional) || hasFlag(expr_, Optional))
            return;

        const Type *target = getOptionalTargetType(retType);
        if (nodeIs(expr, NullLit)) {
            if (!transformOptionalNone(visitor, expr, target))
                node->type = ERROR_TYPE(ctx);
        }
        else {
            expr->type = target;
            if (!transformOptionalSome(
                    visitor, expr, copyAstNode(ctx->pool, expr)))
                node->type = ERROR_TYPE(ctx);
        }
    }
    else {
        node->type = expr_;
        ret = makeTypeReferenceNode(ctx->pool, expr_, &node->loc);
        if (nodeIs(func, FuncDecl))
            func->funcDecl.signature->ret = ret;
        else
            func->closureExpr.ret = ret;
    }
}

static void checkYieldStmt(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *block = ctx->catcher.block, *lhs = ctx->catcher.expr,
            *expr = node->yieldStmt.expr;
    csAssert0(block);
    if (isVoidResultType(lhs->type)) {
        logError(ctx->L,
                 &node->loc,
                 "yield statement not allowed in catch for expressions that "
                 "return void",
                 NULL);
        node->type = ERROR_TYPE(ctx);
        return;
    }

    const Type *type = checkType(visitor, expr);
    if (typeIs(type, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }
    node->type = type;

    const Type *target = getResultTargetType(lhs->type);
    if (block->type == NULL) {
        if (!isTypeAssignableFrom(lhs->type, type)) {
            logError(ctx->L,
                     &node->loc,
                     "catch yield value '{t}' not assignable to expression "
                     "type '{t}'",
                     (FormatArg[]){{.t = type}, {.t = target}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
        block->type = type;
        node->type = type;
    }
    else if (typeIs(block->type, Error)) {
        return;
    }

    if (!isTypeCastAssignable(block->type, type)) {
        logError(ctx->L,
                 &node->loc,
                 "inconsistent yield type, expecting `{t}` (deduced from first "
                 "yield expression) got `t`",
                 (FormatArg[]){{.t = block->type}, {.t = type}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (ctx->catcher.variable == NULL) {
        ctx->catcher.variable =
            makeVarDecl(ctx->pool,
                        &node->loc,
                        flgTemporary,
                        makeAnonymousVariable(ctx->strings, "_Tres"),
                        makeTypeReferenceNode(ctx->pool, target, &node->loc),
                        NULL,
                        NULL,
                        target);
    }
    node->tag = astExprStmt;
    clearAstBody(node);
    node->exprStmt.expr =
        makeAssignExpr(ctx->pool,
                       &node->loc,
                       flgNone,
                       makeResolvedPath(ctx->pool,
                                        &node->loc,
                                        ctx->catcher.variable->_name,
                                        flgNone,
                                        ctx->catcher.variable,
                                        NULL,
                                        target),
                       opAssign,
                       expr,
                       NULL,
                       target);
    node->type = target;
}

static void checkDeferStmt(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *type = checkType(visitor, node->deferStmt.stmt);

    if (typeIs(type, Error)) {
        node->type = type;
        return;
    }
    node->type = makeVoidType(ctx->types);
}

static void checkBreakOrContinueStmt(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    node->type = makeVoidType(ctx->types);
}

static void checkWhileStmt(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *cond = node->whileStmt.cond, *body = node->whileStmt.body,
            *update = node->whileStmt.update;

    const Type *cond_ = checkType(visitor, cond);
    if (typeIs(cond_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    cond_ = unwrapType(cond_, NULL);
    if (isClassOrStructType(stripReference(cond_))) {
        if (!transformToTruthyOperator(visitor, cond)) {
            if (!typeIs(cond->type, Error))
                logError(ctx->L,
                         &cond->loc,
                         "expecting a struct that overloads the truthy `!!` in "
                         "an if statement condition, "
                         "got '{t}'",
                         (FormatArg[]){{.t = cond_}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
        cond_ = cond->type;
    }

    if (!isTruthyType(cond_)) {
        logError(ctx->L,
                 &cond->loc,
                 "expecting a truthy type in an `while` "
                 "statement condition, "
                 "got '{t}'",
                 (FormatArg[]){{.t = cond_}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    bool currentReturnState = ctx->returnState;
    ctx->returnState = false;
    const Type *body_ = checkType(visitor, body);
    ctx->returnState = currentReturnState;

    if (typeIs(body_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    node->type = body_;
    if (update) {
        body_ = checkType(visitor, update);
        if (typeIs(body_, Error)) {
            node->type = ERROR_TYPE(ctx);
        }
    }
}

static void checkStringExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *part = node->stringExpr.parts;

    for (; part; part = part->next) {
        part->type = checkType(visitor, part);
        if (typeIs(part->type, Error)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }
    }

    part = node->stringExpr.parts;
    if (nodeIs(part, StringLit) && part->next == NULL) {
        node->tag = astStringLit;
        node->type = part->type;
        memcpy(&node->_body, &part->_body, CXY_AST_NODE_BODY_SIZE);
    }
    else {
        node->type = makeStringType(ctx->types);
    }
}

static void checkSpreadExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->spreadExpr.expr;
    bool isMove = nodeIs(expr, UnaryExpr) && expr->unaryExpr.op == opMove;
    if (isMove)
        expr = expr->unaryExpr.operand;

    if (nodeIs(expr, TupleExpr)) {
        astVisitManyNodes(visitor, expr->tupleExpr.elements);
        getLastAstNode(expr->tupleExpr.elements)->next = node->next;
        *node = *expr->tupleExpr.elements;
        if (isMove) {
            for (expr = node; expr; expr = expr->next) {
                if (nodeIsLeftValue(expr)) {
                    AstNode *move = shallowCloneAstNode(ctx->pool, expr);
                    expr->tag = astUnaryExpr;
                    expr->unaryExpr.isPrefix = true;
                    expr->unaryExpr.op = opMove;
                    expr->unaryExpr.operand = move;
                }
            }
        }
        return;
    }

    const Type *type = checkType(visitor, expr);
    if (typeIs(type, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (isVoidType(type)) {
        node->tag = astNoop;
        node->type = type;
        return;
    }

    if (hasFlag(expr, Variadic)) {
        expr->type = type;
        csAssert0(nodeIs(expr, Path));
        AstNode *param = getResolvedPath(expr);
        csAssert0(nodeIs(param, FuncParamDecl));
        if (param->next == NULL) {
            if (isMove) {
                node->unaryExpr.operand = expr;
                node->tag = astUnaryExpr;
                node->unaryExpr.op = opMove;
                node->unaryExpr.isPrefix = true;
                node->type = type;
            }
            else {
                replaceAstNode(node, expr);
            }
            return;
        }

        if (isMove) {
            expr->unaryExpr.operand = shallowCloneAstNode(ctx->pool, expr);
            expr->tag = astUnaryExpr;
            expr->unaryExpr.op = opMove;
            expr->unaryExpr.isPrefix = true;
        }

        param = param->next;
        AstNode *first = expr, *arg = expr;
        while (param) {
            arg->next = makeResolvedPath(ctx->pool,
                                         &param->loc,
                                         param->funcParam.name,
                                         flgNone,
                                         param,
                                         NULL,
                                         param->type);
            if (isMove) {
                arg->next = makeUnaryExpr(ctx->pool,
                                          &param->loc,
                                          flgNone,
                                          true,
                                          opMove,
                                          arg->next,
                                          NULL,
                                          param->type);
            }
            arg = arg->next;
            param = param->next;
        }
        replaceAstNode(node, first);
        return;
    }

    if (!typeIs(type, Tuple)) {
        logError(ctx->L,
                 &node->loc,
                 "spread operator `...` can only be used "
                 "with tuple expressions",
                 NULL);
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (type->tuple.count == 0) {
        node->tag = astNoop;
        // node->type = makeTupleType(ctx->types, NULL, 0, flgNone);
        return;
    }

    AstNode *variable = makeSpreadVariable(ctx, expr), *parts = NULL,
            *it = NULL;
    for (u64 i = 0; i < type->tuple.count; i++) {
        const Type *type_ = type->tuple.members[i];
        AstNode *tmp = makeAstNode(
            ctx->pool,
            &node->loc,
            &(AstNode){
                .tag = astMemberExpr,
                .flags =
                    type->flags | type_->flags | (isMove ? flgMove : flgNone),
                .type = type_,
                .memberExpr = {
                    .target = nodeIs(variable, Path)
                                  ? copyAstNode(ctx->pool, variable)
                                  : makePathFromIdent(ctx->pool,
                                                      variable->varDecl.names),
                    .member = makeUnsignedIntegerLiteral(
                        ctx->pool,
                        &node->loc,
                        i,
                        NULL,
                        getPrimitiveType(ctx->types, prtI64))}});
        if (parts == NULL)
            parts = it = tmp;
        else
            it = it->next = tmp;
    }

    it->next = node->next;
    *node = *parts;
    if (variable != expr)
        astModifierAdd(&ctx->blockModifier, variable);
}

static void checkExprStmt(AstVisitor *visitor, AstNode *node)
{
    node->exprStmt.expr->parentScope = node;
    node->type = checkType(visitor, node->exprStmt.expr);
}

static void checkStmtExpr(AstVisitor *visitor, AstNode *node)
{
    node->type = checkType(visitor, node->stmtExpr.stmt);
}

static void checkGroupExpr(AstVisitor *visitor, AstNode *node)
{
    AstNode *expr = node->groupExpr.expr;
    if (nodeIs(expr, BlockStmt)) {
        node->tag = astStmtExpr;
        expr->flags |= flgBlockReturns;
    }
    node->type = checkType(visitor, node->stmtExpr.stmt);
}

static void checkMacroCallExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    if (!evaluate(ctx->evaluator, node))
        node->type = ERROR_TYPE(ctx);
}

static void checkRef(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *referred = node->reference.target;
    if (referred->type)
        node->type = referred->type;
    else
        node->type = checkType(visitor, referred);
}

static void checkProgram(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *decl = node->program.decls;
    astModifierInit(&ctx->root, node);
    astVisit(visitor, node->program.module);
    astVisitManyNodes(visitor, node->program.top);

    bool isBuiltinModule = hasFlag(node, BuiltinsModule);
    for (; decl; decl = decl->next) {
        astModifierNext(&ctx->root, decl);
        decl->flags |= (isBuiltinModule ? flgBuiltin : flgNone);
        astVisit(visitor, decl);
        if (decl->tag == astBlockStmt) {
            printf("We can't have block\n");
        }
    }

    if (isBuiltinModule || node->program.module) {
        buildModuleType(ctx->types, node, isBuiltinModule);
    }
}

static void withSavedStack(Visitor func, AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    __typeof(ctx->stack) stack = ctx->stack;

    if (!hasFlag(node, Visited)) {
        func(visitor, node);
    }

    // clear the visited flag
    node->flags &= ~flgVisited;

    ctx->stack = stack;
}

static void checkInlineAssembly(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *input = node->inlineAssembly.inputs,
            *output = node->inlineAssembly.outputs;

    for (; input; input = input->next) {
        input->type = checkType(visitor, input->asmOperand.operand);
        if (typeIs(input->type, Error)) {
            node->type = ERROR_TYPE(ctx);
        }
    }
    if (typeIs(node->type, Error))
        return;

    for (; output; output = output->next) {
        output->type = checkType(visitor, output->asmOperand.operand);
        if (typeIs(output->type, Error)) {
            node->type = ERROR_TYPE(ctx);
        }
    }
}

static void checkField(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *type =
        checkType(visitor, node->structField.type) ?: makeAutoType(ctx->types);
    if (typeIs(type, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (node->structField.value)
        node->structField.value->parentScope = node;

    const Type *value = checkType(visitor, node->structField.value);
    if (typeIs(value, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (value && !isTypeAssignableFrom(type, value)) {
        logError(ctx->L,
                 &node->structField.value->loc,
                 "field initializer of type '{t}' not compatible with "
                 "field type '{t}'",
                 (FormatArg[]){{.t = value}, {.t = type}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    node->type = typeIs(type, Auto) ? value : type;
}

static void checkLiteral(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    switch (node->tag) {
    case astNullLit:
        node->type = makeNullType(ctx->types);
        break;
    case astBoolLit:
        node->type = getPrimitiveType(ctx->types, prtBool);
        break;
    case astCharLit:
        node->type = getPrimitiveType(ctx->types, prtChar);
        break;
    case astIntegerLit:
        node->type =
            getIntegerTypeForLiteral(ctx->types, integerLiteralValue(node));
        break;
    case astFloatLit:
        node->type = getPrimitiveType(ctx->types, prtF64);
        break;
    case astStringLit:
        node->type = makeStringType(ctx->types);
        break;
    default:
        unreachable("NOT LITERAL");
    }
}

static void checkTernaryExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *cond = checkType(visitor, node->ternaryExpr.cond);
    const Type *body = checkType(visitor, node->ternaryExpr.body);
    const Type *otherwise = checkType(visitor, node->ternaryExpr.otherwise);

    if (typeIs(cond, Error) || //
        typeIs(body, Error) || //
        typeIs(otherwise, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (!isTruthyType(cond)) {
        logError(ctx->L,
                 &node->ternaryExpr.cond->loc,
                 "expecting a ternary expression ('?') condition type to be "
                 "truthy evaluable, "
                 "got '{t}'",
                 (FormatArg[]){{.t = cond}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (!isTypeAssignableFrom(body, otherwise)) {
        logError(ctx->L,
                 &node->loc,
                 "operands to ternary expression ('?') have different types, "
                 "'{t}' and '{t}'",
                 (FormatArg[]){{.t = body}, {.t = otherwise}});
        node->type = ERROR_TYPE(ctx);
    }
    else
        node->type = body;
}

const FileLoc *manyNodesLoc_(FileLoc *dst, AstNode *nodes)
{
    if (nodes == NULL)
        return NULL;

    if (nodes->next == NULL) {
        *dst = nodes->loc;
        return dst;
    }

    return locExtend_(dst, &nodes->loc, &getLastAstNode(nodes)->loc);
}

const FileLoc *lastNodeLoc_(FileLoc *dst, AstNode *nodes)
{
    if (nodes == NULL)
        return NULL;
    *dst = getLastAstNode(nodes)->loc;
    return dst;
}

const Type *checkMaybeComptime(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    if (hasFlag(node, Comptime) && !evaluate(ctx->evaluator, node)) {
        node->type = ERROR_TYPE(ctx);
    }

    return checkType(visitor, node);
}

AstNode *checkAst(CompilerDriver *driver, AstNode *node)
{
    cstring ns =
        node->program.module ? node->program.module->moduleDecl.name : NULL;
    TypingContext context = {.L = driver->L,
                             .pool = driver->pool,
                             .strings = driver->strings,
                             .types = driver->types,
                             .traceMemory = driver->options.withMemoryTrace &
                                            isBuiltinsInitialized(),
                             .exceptionTrace =
                                 driver->options.debug ||
                                 driver->options.optimizationLevel != O3,
                             .path = node->loc.fileName,
                             .mod = ns};

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context, {
        [astProgram] = checkProgram,
        [astTypeRef] = checkTypeRef,
        [astNullLit] = checkLiteral,
        [astBoolLit] = checkLiteral,
        [astCharLit] = checkLiteral,
        [astIntegerLit] = checkLiteral,
        [astFloatLit] = checkLiteral,
        [astStringLit] = checkLiteral,
        [astPrimitiveType] = checkBuiltinType,
        [astStringType] = checkBuiltinType,
        [astAutoType] = checkBuiltinType,
        [astVoidType] = checkBuiltinType,
        [astPointerType] = checkPointerType,
        [astReferenceType] = checkReferenceType,
        [astTupleType] = checkTupleType,
        [astFuncType] = checkFunctionType,
        [astArrayType] = checkArrayType,
        [astOptionalType] = checkOptionalType,
        [astResultType] = checkResultType,
        [astDefine] = checkDefine,
        [astIdentifier] = checkIdentifier,
        [astPath] = checkPath,
        [astFuncDecl] = checkFunctionDecl,
        [astMacroDecl] = astVisitSkip,
        [astFuncParamDecl] = checkFunctionParam,
        [astVarDecl] = checkVarDecl,
        [astTypeDecl] = checkTypeDecl,
        [astUnionDecl] = checkUnionDecl,
        [astEnumDecl] = checkEnumDecl,
        [astGenericDecl] = checkGenericDecl,
        [astAsm] = checkInlineAssembly,
        [astFieldDecl] = checkField,
        [astStructDecl] = checkStructDecl,
        [astClassDecl] = checkClassDecl,
        [astInterfaceDecl] = checkInterfaceDecl,
        [astException] = checkExceptionDecl,
        [astImportDecl] = astVisitSkip,
        [astReturnStmt] = checkReturnStmt,
        [astYieldStmt] = checkYieldStmt,
        [astBlockStmt] = checkBlockStmt,
        [astDeferStmt] = checkDeferStmt,
        [astBreakStmt] = checkBreakOrContinueStmt,
        [astContinueStmt] = checkBreakOrContinueStmt,
        [astIfStmt] = checkIfStmt,
        [astWhileStmt] = checkWhileStmt,
        [astForStmt] = checkForStmt,
        [astExprStmt] = checkExprStmt,
        [astCaseStmt] = checkCaseStmt,
        [astSwitchStmt] = checkSwitchStmt,
        [astMatchStmt] = checkMatchStmt,
        [astStringExpr] = checkStringExpr,
        [astCallExpr] = checkCallExpr,
        [astTupleExpr] = checkTupleExpr,
        [astMemberExpr] = checkMemberExpr,
        [astSpreadExpr] = checkSpreadExpr,
        [astBinaryExpr] = checkBinaryExpr,
        [astUnaryExpr] = checkUnaryExpr,
        [astPointerOf] = checkPointerOfExpr,
        [astReferenceOf] = checkReferenceOfExpr,
        [astAssignExpr] = checkAssignExpr,
        [astIndexExpr] = checkIndexExpr,
        [astStructExpr] = checkStructExpr,
        [astRangeExpr] = checkRangeExpr,
        [astNewExpr] = checkNewExpr,
        [astCastExpr] = checkCastExpr,
        [astTypedExpr] = checkTypedExpr,
        [astStmtExpr] = checkStmtExpr,
        [astGroupExpr] = checkGroupExpr,
        [astClosureExpr] = checkClosureExpr,
        [astArrayExpr] = checkArrayExpr,
        [astMacroCallExpr] = checkMacroCallExpr,
        [astTernaryExpr] = checkTernaryExpr,
        [astRef] = checkRef
    }, .fallback = astVisitFallbackVisitAll, .dispatch = withSavedStack);
    // clang-format on

    EvalContext evalContext = {.L = driver->L,
                               .pool = driver->pool,
                               .strings = driver->strings,
                               .types = driver->types,
                               .typer = &visitor};
    AstVisitor evaluator;
    initEvalVisitor(&evaluator, &evalContext);
    context.evaluator = &evaluator;

    context.types->currentNamespace = ns;

    astVisit(&visitor, node);

    context.types->currentNamespace = NULL;

    return node;
}
