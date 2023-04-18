//
// Created by Carter on 2023-03-28.
//

#include "check.h"
#include "capture.h"
#include "scope.h"
#include "ttable.h"

#include "semantics.h"

#include "core/alloc.h"
#include "core/utils.h"

#include <string.h>

static void addBuiltinFunc(SemanticsContext *ctx,
                           cstring name,
                           const Type *ret,
                           const Type **params,
                           u64 paramsCount)
{
    AstNode *node = makeAstNode(
        ctx->pool,
        builtinLoc(),
        &(AstNode){.tag = astFuncDecl, .flags = flgBuiltin, .type = NULL});

    defineSymbol(&ctx->env, ctx->L, name, node);

    node->type = makeFuncType(ctx->typeTable,
                              &(Type){.tag = typFunc,
                                      .name = name,
                                      .flags = flgBuiltin,
                                      .func = {.retType = ret,
                                               .params = params,
                                               .paramsCount = paramsCount,
                                               .decl = node}});
}

static void addBuiltinVariable(SemanticsContext *ctx,
                               cstring name,
                               const Type *type,
                               AstNode *value)
{
    defineSymbol(&ctx->env,
                 ctx->L,
                 name,
                 makeAstNode(ctx->pool,
                             builtinLoc(),
                             &(AstNode){.tag = astVarDecl,
                                        .flags = flgBuiltin | flgConst,
                                        .type = type}));
}

static void addBuiltinType(SemanticsContext *ctx,
                           cstring name,
                           u64 flags,
                           const Type *type)
{
    defineSymbol(&ctx->env,
                 ctx->L,
                 name,
                 makeAstNode(ctx->pool,
                             builtinLoc(),
                             &(AstNode){.tag = astTypeDecl,
                                        .flags = flgBuiltin | flags,
                                        .type = type}));
}

static void initBuiltins(SemanticsContext *ctx)
{
    {
        const Type *params[] = {getPrimitiveType(ctx->typeTable, prtChar)};
        addBuiltinFunc(
            ctx, "wputc", getPrimitiveType(ctx->typeTable, prtI32), params, 1);
    }

    {
        addBuiltinType(
            ctx, "char", flgNative, makeOpaqueType(ctx->typeTable, "char"));

        addBuiltinType(ctx,
                       "cxy_range_t",
                       flgNative,
                       makeOpaqueType(ctx->typeTable, "cxy_range_t"));
    }
}

static void checkProgram(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    pushScope(&ctx->env, node);

    initBuiltins(ctx);

    ctx->previousTopLevelDecl = node->program.decls;
    for (AstNode *decl = node->program.decls; decl; decl = decl->next) {
        ctx->currentTopLevelDecl = decl;
        astVisit(visitor, decl);
        ctx->previousTopLevelDecl = decl;
    }
}

static void checkFallback(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    switch (node->tag) {
    case astExprStmt:
        node->type = evalType(visitor, node->exprStmt.expr);
        break;
    case astStmtExpr:
        node->type = evalType(visitor, node->stmtExpr.stmt);
        break;
    default:
        node->type = makeVoidType(ctx->typeTable);
    }
}

static void checkFuncParam(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    if (node->parentScope == NULL || node->parentScope->tag != astFuncType)
        defineSymbol(&ctx->env, ctx->L, node->funcParam.name, node);

    if (node->funcParam.type)
        node->type = evalType(visitor, node->funcParam.type);
    else
        csAssert0(node->type);

    if (node->funcParam.def) {
        const Type *def = evalType(visitor, node->funcParam.def);
        if (!isTypeAssignableFrom(node->type, def)) {
            logError(ctx->L,
                     &node->funcParam.def->loc,
                     "parameter default value type '{t}' not compatible with "
                     "parameter type '{t}",
                     (FormatArg[]){{.t = def}, {.t = node->type}});
        }
    }
}

static const Type *transformFuncTypeParam(SemanticsContext *ctx,
                                          const Type *type)
{
    // change (i32) => X, (&void, i32) => X
    const Type **newParams =
        mallocOrDie(sizeof(Type *) * type->func.paramsCount + 1);
    newParams[0] = makeVoidPointerType(ctx->typeTable, flgNone);
    for (u64 i = 0; i < type->func.paramsCount; i++)
        newParams[i + 1] = type->func.params[i];

    type =
        makeFuncType(ctx->typeTable,
                     &(Type){.tag = typFunc,
                             .flags = type->flags | flgFuncTypeParam,
                             .func = {.params = newParams,
                                      .paramsCount = type->func.paramsCount + 1,
                                      .retType = type->func.retType}});
    type = makeTupleType(
        ctx->typeTable,
        (const Type *[]){makeVoidPointerType(ctx->typeTable, flgNone), type},
        2,
        type->flags | flgFuncTypeParam);

    return type;
}

static void checkCastExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *expr = evalType(visitor, node->castExpr.expr);
    const Type *target = evalType(visitor, node->castExpr.to);
    if (!isTypeCastAssignable(target, expr)) {
        logError(ctx->L,
                 &node->loc,
                 "type '{t}' cannot be cast to type '{t}'",
                 (FormatArg[]){{.t = expr}, {.t = target}});
    }
    node->type = target;
}

static void checkRangeExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *i64Type = getPrimitiveType(ctx->typeTable, prtI64);

    const Type *start = evalType(visitor, node->rangeExpr.start);
    const Type *end = evalType(visitor, node->rangeExpr.end);

    if (!isTypeAssignableFrom(i64Type, start)) {
        logError(ctx->L,
                 &node->rangeExpr.start->loc,
                 "expecting integer type for range start expression, got '{t}'",
                 (FormatArg[]){{.t = start}});
        start = ERROR_TYPE(ctx);
    }

    if (!isTypeAssignableFrom(i64Type, end)) {
        logError(ctx->L,
                 &node->rangeExpr.start->loc,
                 "expecting integer type for range end expression, got '{t}'",
                 (FormatArg[]){{.t = end}});
        start = ERROR_TYPE(ctx);
    }

    if (node->rangeExpr.step) {
        const Type *step = evalType(visitor, node->rangeExpr.step);
        if (!isTypeAssignableFrom(i64Type, end)) {
            logError(
                ctx->L,
                &node->rangeExpr.start->loc,
                "expecting integer type for range step expression, got '{t}'",
                (FormatArg[]){{.t = step}});
            start = ERROR_TYPE(ctx);
        }
    }

    node->type = makeOpaqueType(ctx->typeTable, "cxy_range_t");
}

static void checkTernaryExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *cond = evalType(visitor, node->ternaryExpr.cond);
    const Type *body = evalType(visitor, node->ternaryExpr.body);
    const Type *otherwise = evalType(visitor, node->ternaryExpr.otherwise);

    if (!isTypeAssignableFrom(getPrimitiveType(ctx->typeTable, prtBool),
                              cond)) {
        logError(ctx->L,
                 &node->ternaryExpr.cond->loc,
                 "expecting a ternary expression ('?') condition type of bool, "
                 "got '{t}'",
                 (FormatArg[]){{.t = cond}});
        node->type = ERROR_TYPE(ctx);
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

static void checkNewExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *type = NULL, *init = NULL;
    node->flags |= flgNewAllocated;

    if (node->newExpr.type)
        type = evalType(visitor, node->newExpr.type);

    if (node->newExpr.init)
        init = evalType(visitor, node->newExpr.init);

    if (type == NULL) {
        type = init;
    }

    if (init && !isTypeAssignableFrom(type, init)) {
        logError(
            ctx->L,
            &node->loc,
            "new initializer value type '{t}' is not assignable to type '{t}'",
            (FormatArg[]){{.t = type}, {.t = init}});
    }
    node->flags = (node->newExpr.type ? node->newExpr.type->flags
                                      : node->newExpr.init->flags);
    node->type =
        makePointerType(ctx->typeTable, type, type->flags | flgNewAllocated);
}

static void checkVarDecl(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *names = node->varDecl.names;

    defineSymbol(&ctx->env, ctx->L, names->ident.value, node);
    if (node->varDecl.type) {
        node->varDecl.type->flags |= node->flags;
        node->type = evalType(visitor, node->varDecl.type);
    }
    else {
        node->type = makeAutoType(ctx->typeTable);
    }

    const Type *value = NULL;
    if (node->varDecl.init) {
        value = evalType(visitor, node->varDecl.init);
        if (value->tag == typArray && node->varDecl.init->tag != astArrayExpr) {
            logError(ctx->L,
                     &node->varDecl.init->loc,
                     "initializer for array declaration can only be an array "
                     "expression",
                     NULL);
            node->type = ERROR_TYPE(ctx);
        }
        else if (!isTypeAssignableFrom(node->type, value)) {
            logError(ctx->L,
                     &node->varDecl.init->loc,
                     "incompatible types, expecting type '{t}', got '{t}'",
                     (FormatArg[]){{.t = node->type}, {.t = value}});
            node->type = ERROR_TYPE(ctx);
        }
        else if ((value->tag == typPointer) &&
                 ((value->flags & flgConst) && !(node->flags & flgConst))) {
            logError(ctx->L,
                     &node->varDecl.init->loc,
                     "assigning a const pointer to a non-const variable "
                     "discards const qualifier",
                     NULL);
            node->type = ERROR_TYPE(ctx);
        }
        if (node->type->tag == typAuto)
            node->type = value;
    }
}

static void checkIdentifier(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *symbol =
        findSymbol(&ctx->env, ctx->L, node->ident.value, &node->loc);
    if (symbol == NULL)
        node->type = ERROR_TYPE(ctx);
    else
        node->type = symbol->type;
}

static void checkPathElement(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    csAssert0(node->parentScope);
    const Type *scope = stripPointer(node->parentScope->type);

    const Env *env = NULL;
    Env thisEnv = {};
    switch (scope->tag) {
    case typEnum:
        env = scope->tEnum.env;
        break;
    case typStruct:
        env = scope->tStruct.env;
        break;
    case typThis:
        thisEnv = (Env){.first = ctx->env.first, .scope = ctx->env.first};
        env = &thisEnv;
        break;
    default:
        logError(ctx->L,
                 &node->loc,
                 "type '{t}' does not support member expressions",
                 (FormatArg[]){{.t = scope}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    AstNode *symbol =
        findSymbol(env, ctx->L, node->pathElement.name, &node->loc);

    if (symbol == NULL) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    node->flags = symbol->flags;
    node->type = symbol->type;
    switch (scope->tag) {
    case typEnum:
        if (node->parentScope->flags & flgMember) {
            logError(ctx->L,
                     &node->loc,
                     "member expression not supported on enum members",
                     NULL);
            node->type = ERROR_TYPE(ctx);
        }
        else {
            node->type = scope;
        }
        break;
    default:
        break;
    }
}

static const Type *checkFirstPathElement(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    Scope *scope = NULL, *closure = ctx->closure;
    AstNode *symbol = findSymbolAndScope(
        &ctx->env, ctx->L, node->pathElement.name, &node->loc, &scope);
    u64 flags = flgNone;
    if (symbol == NULL) {
        node->type = ERROR_TYPE(ctx);
        return ERROR_TYPE(ctx);
    }
    if (scope->node && scope->node->tag == astStructDecl) {
        node->flags = flgAddThis;
        if (scope != ctx->env.first)
            node->flags |= flgAddSuper;
    }

    node->type = symbol->type;
    flags = (symbol->flags & (flgConst | flgAddThis));
    node->flags |= flags;
    if (closure == NULL)
        // We are outside a closure
        return node->type;
    if (isRootScope(scope) && isInSameEnv(closure, scope))
        // Symbol defined in global scope
        return node->type;
    if (!isAncestorScope(scope, closure))
        // Symbol not defined in parent scope of closure
        return node->type;

    node->pathElement.index = addClosureCapture(
        &closure->node->closureExpr.capture,
        node->pathElement.name,
        (node->type->tag == typPrimitive || node->type->tag == typPointer)
            ? node->type
            : makePointerType(ctx->typeTable,
                              node->type,
                              (flags & flgConst) | flgCapturePointer));

    node->flags |= flgCapture;

    return node->type;
}

static void checkPath(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    AstNode *elem = node->path.elements;
    const Type *type = checkFirstPathElement(visitor, elem);
    u64 flags = elem->flags;
    AstNode *prev = elem;
    elem = elem->next;
    for (; elem; elem = elem->next) {
        elem->parentScope = prev;
        if ((type = evalType(visitor, elem)) == ERROR_TYPE(ctx)) {
            node->type = ERROR_TYPE(ctx);
            return;
        };
        flags = elem->flags;
        prev = elem;
    }

    node->type = type;
    node->flags |= flags;
}

static void checkBinary(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *left = evalType(visitor, node->binaryExpr.lhs);
    const Type *right = evalType(visitor, node->binaryExpr.rhs);
    // TODO check compatibility
    enum {
        optInvalid = -1,
        optNumeric,
        optInteger,
        optLogical,
        optComparison,
        optEquality,
        optRange,
    } opKind = optInvalid;

    Operator op = node->binaryExpr.op;
    if (stripPointer(left)->tag == typStruct) {
        cstring name = getBinaryOpFuncName(op);
        const Type *target = stripPointer(left);
        AstNode *overload = findSymbolOnly(target->tStruct.env, name);
        if (overload) {
            AstNode *callee = makeAstNode(
                ctx->pool,
                &node->binaryExpr.lhs->loc,
                &(AstNode){
                    .tag = astMemberExpr,
                    .type = overload->type,
                    .memberExpr = {
                        .target = node->binaryExpr.lhs,
                        .member = makeAstNode(
                            ctx->pool,
                            &node->binaryExpr.lhs->loc,
                            &(AstNode){.tag = astIdentifier,
                                       .type = overload->type,
                                       .flags = node->binaryExpr.lhs->flags,
                                       .ident.value = name})}});
            AstNode *args =
                right->tag == typPointer
                    ? node->binaryExpr.rhs
                    : makeAstNode(
                          ctx->pool,
                          &node->binaryExpr.rhs->loc,
                          &(AstNode){
                              .tag = astAddressOf,
                              .flags = node->binaryExpr.rhs->flags,
                              .unaryExpr = {.op = opAddrOf,
                                            .operand = node->binaryExpr.rhs,
                                            .isPrefix = true}});
            memset(&node->binaryExpr, 0, sizeof(node->binaryExpr));
            node->tag = astCallExpr;
            node->callExpr.callee = callee;
            node->callExpr.args = args;
            evalType(visitor, node);
            return;
        }
    }
    const Type *type = promoteType(ctx->typeTable, left, right);
    node->type = ERROR_TYPE(ctx);

    switch (op) {
        // Numeric arithmetic
#define f(O, ...) case op##O:
        AST_ARITH_EXPR_LIST(f)
        opKind = optNumeric;
        break;

        AST_BIT_EXPR_LIST(f)
        AST_SHIFT_EXPR_LIST(f)
        opKind = optInteger;
        break;

        AST_LOGIC_EXPR_LIST(f)
        opKind = optLogical;
        break;

        AST_CMP_EXPR_LIST(f)
        opKind = (op == opEq || op == opNe) ? optEquality : optComparison;
        break;
    case opRange:
        opKind = optRange;
        break;
    default:
        unreachable("");
    }

    if (type == NULL) {
        logError(ctx->L,
                 &node->loc,
                 "binary operation '{s}' between type '{t}' and '{t}' is not "
                 "supported",
                 (FormatArg[]){
                     {.s = getBinaryOpString(op)}, {.t = left}, {.t = right}});
        return;
    }

    switch (opKind) {
    case optNumeric:
        if (!isNumericType(type)) {
            logError(ctx->L,
                     &node->loc,
                     "cannot perform binary operation '{s}' on non-numeric "
                     "type '{t}'",
                     (FormatArg[]){{.s = getBinaryOpString(op)}, {.t = type}});
            return;
        }
        node->type = type;
        break;
    case optInteger:
        if (!isIntegerType(type)) {
            logError(ctx->L,
                     &node->loc,
                     "cannot perform binary operation '{s}' on non-integer "
                     "type '{t}'",
                     (FormatArg[]){{.s = getBinaryOpString(op)}, {.t = type}});
            return;
        }
        node->type = type;
        break;
    case optLogical:
        if (type != getPrimitiveType(ctx->typeTable, prtBool)) {
            logError(ctx->L,
                     &node->loc,
                     "cannot perform logical binary operation '{s}' on "
                     "non-boolean "
                     "type '{t}'",
                     (FormatArg[]){{.s = getBinaryOpString(op)}, {.t = type}});
            return;
        }
        node->type = type;
        break;
    case optComparison:
        if (!isNumericType(type)) {
            logError(ctx->L,
                     &node->loc,
                     "cannot perform comparison binary operation '{s}' on "
                     "non-numeric "
                     "type '{t}'",
                     (FormatArg[]){{.s = getBinaryOpString(op)}, {.t = type}});
            return;
        }
        node->type = getPrimitiveType(ctx->typeTable, prtBool);
        ;
        break;
    case optEquality:
        if (type->tag != typPrimitive && type->tag != typPointer &&
            type->tag != typString) {
            logError(ctx->L,
                     &node->loc,
                     "cannot perform equality binary operation '{s}' on "
                     "type '{t}'",
                     (FormatArg[]){{.s = getBinaryOpString(op)}, {.t = type}});
            return;
        }
        node->type = getPrimitiveType(ctx->typeTable, prtBool);
        break;
    case optRange: {
        if (!isIntegerType(left)) {
            logError(ctx->L,
                     &node->loc,
                     "expecting an integral type for range expression "
                     "start, got "
                     "type '{t}'",
                     (FormatArg[]){{.t = left}});
            return;
        }
        if (!isIntegerType(right)) {
            logError(ctx->L,
                     &node->loc,
                     "expecting an integral type for range expression end, got "
                     "type '{t}'",
                     (FormatArg[]){{.t = left}});
        }
        AstNode binary = *node;
        memset(&node->binaryExpr, 0, sizeof(node->binaryExpr));
        node->tag = astRangeExpr;
        node->rangeExpr.start = binary.binaryExpr.lhs;
        node->rangeExpr.end = binary.binaryExpr.rhs;
        node->rangeExpr.step = NULL;
        node->type = makeOpaqueType(ctx->typeTable, "cxy_range_t");
        break;
    }
    default:
        unreachable("");
    }
}

static const Type *checkPrefixExpr(SemanticsContext *ctx,
                                   const Type *operand,
                                   AstNode *node)
{
    switch (node->unaryExpr.op) {
    case opPreDec:
    case opPreInc:
        if (node->flags & flgConst) {
            logError(
                ctx->L,
                &node->loc,
                "prefix operation '{s}' cannot be performed on a constant",
                (FormatArg[]){{.s = getUnaryOpString(node->unaryExpr.op)}});
            operand = ERROR_TYPE(ctx);
            break;
        }
    case opMinus:
    case opPlus:
        if (!isNumericType(operand)) {
            logError(ctx->L,
                     &node->unaryExpr.operand->loc,
                     "postfix expression '{s}' no supported on type '{t}'",
                     (FormatArg[]){{.s = getUnaryOpString(node->unaryExpr.op)},
                                   {.t = operand}});
            operand = ERROR_TYPE(ctx);
        }
        break;
    case opNot:
        if (operand == getPrimitiveType(ctx->typeTable, prtBool)) {
            logError(ctx->L,
                     &node->unaryExpr.operand->loc,
                     "logical '!' operator no supported on type '{t}', "
                     "expecting bool type",
                     (FormatArg[]){{.t = operand}});
            operand = ERROR_TYPE(ctx);
        }
        break;
    case opDeref:
        if (operand->tag != typPointer) {
            logError(ctx->L,
                     &node->unaryExpr.operand->loc,
                     "cannot not dereference an non-pointer type '{t}'",
                     (FormatArg[]){{.t = operand}});
            operand = ERROR_TYPE(ctx);
        }
        else {
            node->flags |=
                (operand->flags | node->unaryExpr.operand->flags & flgConst);
            operand = operand->pointer.pointed;
        }
        break;
    case opDelete:
        if (operand->tag != typPointer || !(operand->flags & flgNewAllocated)) {
            logError(ctx->L,
                     &node->loc,
                     "cannot delete an none `new` allocated object",
                     NULL);
            operand = ERROR_TYPE(ctx);
        }
        else {
            operand = makeVoidType(ctx->typeTable);
        }
        break;
    default:
        operand = ERROR_TYPE(ctx);
        break;
    }

    return operand;
}

static void checkUnary(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *operand = evalType(visitor, node->unaryExpr.operand);
    node->flags |= node->unaryExpr.operand->flags;

    if (node->unaryExpr.isPrefix) {
        operand = checkPrefixExpr(ctx, operand, node);
    }
    else {
        if (node->flags & flgConst) {
            logError(
                ctx->L,
                &node->loc,
                "postfix operation '{s}' cannot be performed on a constant",
                (FormatArg[]){{.s = getUnaryOpString(node->unaryExpr.op)}});
            operand = ERROR_TYPE(ctx);
        }
        else if (!isNumericType(operand)) {
            logError(ctx->L,
                     &node->unaryExpr.operand->loc,
                     "postfix expression '{s}' no supported on type '{t}'",
                     (FormatArg[]){{.s = getUnaryOpString(node->unaryExpr.op)},
                                   {.t = operand}});
            operand = ERROR_TYPE(ctx);
        }
    }
    node->type = operand;
}

static void checkAssign(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *left = node->assignExpr.lhs, *right = node->assignExpr.rhs;
    const Type *lhs = evalType(visitor, left);
    const Type *rhs = evalType(visitor, right);
    bool isLeftAuto = lhs == makeAutoType(ctx->typeTable);

    // TODO check r-value-ness
    node->type = ERROR_TYPE(ctx);
    if ((left->flags & flgConst) || (lhs->flags & flgConst)) {
        logError(ctx->L,
                 &node->loc,
                 "lhs of assignment expressions is a constant",
                 (FormatArg[]){{.t = lhs}});
        node->type = ERROR_TYPE(ctx);
    }
    else if (rhs->tag == typArray) {
        if (isLeftAuto)
            logError(ctx->L,
                     &node->loc,
                     "array assignment not allowed, assignment should be done "
                     "at initialisation",
                     NULL);
        else
            logError(
                ctx->L, &node->loc, "assign to an array is not allowed", NULL);
    }
    else if (!isTypeAssignableFrom(lhs, rhs)) {
        logError(ctx->L,
                 &node->assignExpr.rhs->loc,
                 "incompatible types on assigment expression, expecting '{t}', "
                 "got '{t}'",
                 (FormatArg[]){{.t = lhs}, {.t = rhs}});
    }
    if (node->type == ERROR_TYPE(ctx))
        return;

    if (isLeftAuto) {
        csAssert0(left->tag == astPath);
        const char *variable = left->path.elements->pathElement.name;
        AstNode *symbol = findSymbol(&ctx->env, ctx->L, variable, &left->loc);
        csAssert0(symbol);
        symbol->type = rhs;
        node->type = symbol->type;
    }
    else {
        node->type = lhs;
    }
}

static void checkAddressOf(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *operand = evalType(visitor, node->unaryExpr.operand);
    node->flags |= node->unaryExpr.operand->flags;
    node->type = makePointerType(
        ctx->typeTable, operand, node->unaryExpr.operand->flags);
}

static void checkIndex(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *target = evalType(visitor, node->indexExpr.target);
    node->flags |= node->indexExpr.target->flags;

    astVisit(visitor, node->indexExpr.index);
    if (target->tag == typPointer) {
        target = target->pointer.pointed;
        node->indexExpr.target = makeAstNode(
            ctx->pool,
            &node->indexExpr.target->loc,
            &(AstNode){.tag = astUnaryExpr,
                       .type = target,
                       .flags = node->indexExpr.target->flags,
                       .unaryExpr = {.op = opDeref,
                                     .operand = node->indexExpr.target,
                                     .isPrefix = true}});
    }

    if (target->tag == typArray) {
        node->type = target->array.elementType;
    }
    else if (target->tag == typMap) {
        node->type = target->map.value;
    }
    else if (target->tag == typStruct || target->tag == typUnion) {
        // TODO find index operator
        logWarning(ctx->L,
                   &node->indexExpr.target->loc,
                   "indexing into {s} types currently not supported",
                   (FormatArg[]){
                       {.s = target->tag == typStruct ? "struct" : "union"}});
    }
    else {
        logError(ctx->L,
                 &node->loc,
                 "index operator (.[]) not supported on type '{t}'",
                 (FormatArg[]){{.t = target}});
    }
}

static void checkArrayExr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    u64 count = 0;
    const Type *elementType = NULL;
    for (AstNode *elem = node->arrayExpr.elements; elem;
         elem = elem->next, count++) {
        const Type *type = evalType(visitor, elem);
        if (elementType == NULL) {
            elementType = type;
            continue;
        }

        if (!isTypeAssignableFrom(elementType, type)) {
            logError(ctx->L,
                     &elem->loc,
                     "inconsistent array types in array, expecting '{t}', "
                     "got '{t}'",
                     (FormatArg[]){{.t = elementType}, {.t = type}});
        }
    }
    if (elementType == NULL) {
        node->type =
            makeArrayType(ctx->typeTable, makeAutoType(ctx->typeTable), 0);
    }
    else {
        node->type = makeArrayType(ctx->typeTable, elementType, count);
    }
}

static void checkTupleExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    u64 count = countAstNodes(node->tupleExpr.args);
    const Type **args = mallocOrDie(sizeof(Type *) * count);
    AstNode *arg = node->tupleExpr.args;

    for (u64 i = 0; arg; arg = arg->next, i++) {
        args[i] = evalType(visitor, arg);
        if (args[i] == ERROR_TYPE(ctx))
            node->type = ERROR_TYPE(ctx);
    }

    if (node->type == NULL) {
        node->type = makeTupleType(ctx->typeTable, args, count, flgNone);
    }

    free(args);
}

static void checkStructExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *target = evalType(visitor, node->structExpr.left);
    if (target->tag != typStruct) {
        logError(ctx->L,
                 &node->structExpr.left->loc,
                 "unsupported type used with struct initializer, '{t}' is not "
                 "a struct",
                 (FormatArg[]){{.t = target}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    AstNode *field = node->structExpr.fields, *prev = node->structExpr.fields;
    bool *initialized = mallocOrDie(sizeof(bool) * target->tStruct.fieldsCount);
    for (; field; field = field->next) {
        prev = field;
        AstNode *decl =
            findSymbolOnly(target->tStruct.env, field->fieldExpr.name);
        if (decl == NULL && target->tStruct.base) {
            decl = findSymbolOnly(target->tStruct.base->tStruct.env,
                                  field->fieldExpr.name);
            if (decl)
                field->flags |= flgAddSuper;
        }

        if (decl == NULL) {
            logError(
                ctx->L,
                &field->loc,
                "field '{s}' does not exist in target struct type '{t}'",
                ((FormatArg[]){{.s = field->fieldExpr.name}, {.t = target}}));
            node->type = ERROR_TYPE(ctx);
            continue;
        }

        const Type *type = evalType(visitor, field->fieldExpr.value);
        if (!isTypeAssignableFrom(decl->type, type)) {
            logError(ctx->L,
                     &field->fieldExpr.value->loc,
                     "value type '{t}' is not assignable to field type '{t}'",
                     (FormatArg[]){{.t = type}, {.t = decl->type}});
            node->type = ERROR_TYPE(ctx);
        }

        initialized[decl->structField.index] = true;
    }

    if (node->type != ERROR_TYPE(ctx)) {
        for (u64 i = 0; i < target->tStruct.fieldsCount; i++) {
            const AstNode *targetField = target->tStruct.fields[i].decl;
            if (initialized[i] || targetField->type->tag == typFunc ||
                targetField->structField.value == NULL)
                continue;

            prev->next = makeAstNode(
                ctx->pool,
                &prev->loc,
                &(AstNode){
                    .tag = astFieldExpr,
                    .type = targetField->type,
                    .flags = targetField->flags,
                    .fieldExpr = {.name = targetField->structField.name,
                                  .value = targetField->structField.value}});
            prev = prev->next;
        }
        node->type = target;
    }
}

static void checkGroupExpr(AstVisitor *visitor, AstNode *node)
{
    node->type = evalType(visitor, node->groupExpr.expr);
}

static const Type *transformFuncTypeParamCallee(SemanticsContext *ctx,
                                                const Type *type,
                                                AstNode *node)
{
    // b._1
    AstNode *newCallee = makeAstNode(
        ctx->pool,
        &node->callExpr.callee->loc,
        &(AstNode){.tag = astMemberExpr,
                   .type = type->tuple.members[1],
                   .flags = node->callExpr.callee->flags,
                   .memberExpr = {.target = node->callExpr.callee,
                                  .member = makeAstNode(
                                      ctx->pool,
                                      &node->callExpr.callee->loc,
                                      &(AstNode){.tag = astIntegerLit,
                                                 .intLiteral.value = 1})}});
    // b._0
    AstNode *arg = makeAstNode(
        ctx->pool,
        &node->callExpr.callee->loc,
        &(AstNode){.tag = astMemberExpr,
                   .next = node->callExpr.args,
                   .type = type->tuple.members[0],
                   .flags = node->callExpr.callee->flags,
                   .memberExpr = {.target = node->callExpr.callee,
                                  .member = makeAstNode(
                                      ctx->pool,
                                      &node->callExpr.callee->loc,
                                      &(AstNode){.tag = astIntegerLit,
                                                 .intLiteral.value = 0})}});

    node->callExpr.callee = newCallee;
    node->callExpr.args = arg;

    return type->tuple.members[1];
}

static const Type *wrapFuncArgInClosure(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *orig = copyAstNode(ctx->pool, node);
    const Type *type = node->type;
    AstNode *params = copyAstNode(ctx->pool, type->func.decl->funcDecl.params);
    AstNode *param = params;
    AstNode *args = NULL, *arg = NULL;
    for (; param; param = param->next) {
        AstNode *newArg =
            makeAstNode(ctx->pool,
                        &node->loc,
                        &(AstNode){.tag = astIdentifier,
                                   .type = param->type,
                                   .ident.value = param->funcParam.name});
        if (arg == NULL) {
            arg = newArg;
            args = newArg;
        }
        else
            arg->next = newArg;
    }

    memset(&node->path, 0, sizeof(node->path));
    node->tag = astClosureExpr;
    node->closureExpr.params = params;
    node->closureExpr.body =
        makeAstNode(ctx->pool,
                    &node->loc,
                    &(AstNode){.tag = astCallExpr,
                               .type = type,
                               .callExpr = {.callee = orig, .args = args}});

    return evalType(visitor, node);
}

static void checkCall(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *callee = evalType(visitor, node->callExpr.callee);
    AstNode *arg = node->callExpr.args;

    if (callee->flags & flgFuncTypeParam) {
        callee = transformFuncTypeParamCallee(ctx, callee, node);
    }

    if (callee->tag != typFunc) {
        logError(ctx->L,
                 &node->callExpr.callee->loc,
                 "expression of type '{t}' cannot be invoked, expecting a "
                 "function",
                 (FormatArg[]){{.t = callee}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    node->type = callee->func.retType;
    u64 paramsCount = callee->func.paramsCount, i = 0;
    if (callee->flags & (flgClosure | flgFuncTypeParam)) {
        paramsCount--;
        i = 1;
    }

    u64 count = countAstNodes(arg),
        withoutDefaulted = paramsCount - callee->func.defaultValuesCount;
    if (count < withoutDefaulted) {
        logError(ctx->L,
                 &node->loc,
                 "few arguments provided to function of type "
                 "'{t}', expecting at least '{u64}' but got '{u64}'",
                 (FormatArg[]){
                     {.t = callee}, {.u64 = withoutDefaulted}, {.u64 = count}});
        return;
    }

    if (count > paramsCount) {
        logError(
            ctx->L,
            &node->loc,
            "too many arguments provided to function of type "
            "'{t}', at most '{u64}' but got '{u64}'",
            (FormatArg[]){{.t = callee}, {.u64 = paramsCount}, {.u64 = count}});
        return;
    }

    for (; arg; arg = arg->next, i++) {
        const Type *type = evalType(visitor, arg);
        const Type *expected = callee->func.params[i];
        if (expected->flags & flgFuncTypeParam) {
            if (!(type->flags & flgClosure))
                type = wrapFuncArgInClosure(visitor, arg);
            expected = expected->tuple.members[1];
        }
        if (stripPointer(expected)->tag == typThis)
            expected =
                makePointerType(ctx->typeTable,
                                callee->func.decl->parentScope->type,
                                callee->func.decl->parentScope->type->flags);

        if (!isTypeAssignableFrom(expected, type)) {
            logError(ctx->L,
                     &arg->loc,
                     "incompatible argument types, expecting '{t}' but got "
                     "'{t}'",
                     (FormatArg[]){{.t = expected}, {.t = type}});
        }
    }

    if (paramsCount > count) {
        // Add default parameters to function call
        AstNode *param =
            getNodeAtIndex(callee->func.decl->funcDecl.params, count);
        csAssert0(param);

        arg = getLastAstNode(node->callExpr.args);
        for (; param; param = param->next) {
            arg->next = copyAstNode(ctx->pool, param->funcParam.def);
        }
    }
}

static void checkBlock(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *stmt = node->blockStmt.stmts;
    AstNode *prev = stmt;
    for (; stmt; stmt = stmt->next) {
        stmt->parentScope = node;
        const Type *type = evalType(visitor, stmt);
        if (stmt->tag == astReturnStmt) {
            node->type = type;
        }
        if (stmt->tag == astDeferStmt) {
            // add statement to epilogue
            insertAstNode(&node->blockStmt.epilogue, stmt->deferStmt.expr);
            stmt->deferStmt.expr->flags |= flgDeferred;
            unlinkAstNode(&node->blockStmt.stmts, prev, stmt);
        }
        prev = stmt;
    }

    if (node->type == NULL) {
        node->type = makeVoidType(ctx->typeTable);
    }
}

static void checkReturn(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *func = findEnclosingFunc(&ctx->env, ctx->L, &node->loc);
    node->type = node->returnStmt.expr
                     ? evalType(visitor, node->returnStmt.expr)
                     : makeVoidType(ctx->typeTable);
    const Type *ret = NULL;
    if (func) {
        if (func->tag == astFuncDecl && func->funcDecl.ret)
            ret = func->funcDecl.ret->type;
        else if (func->tag == astClosureExpr && func->closureExpr.ret)
            ret = func->closureExpr.ret->type;
    }

    if (ret && !isTypeAssignableFrom(ret, node->type)) {
        logError(ctx->L,
                 &node->returnStmt.expr->loc,
                 "return value of type '{t}' incompatible with function return "
                 "type '{t}",
                 (FormatArg[]){{.t = node->type}, {.t = ret}});
    }
    else if (ctx->lastReturn) {
        // we have already seen a return
        if (!isTypeAssignableFrom(ctx->lastReturn->type, node->type)) {
            logError(
                ctx->L,
                &node->returnStmt.expr->loc,
                "inconsistent return types in auto function, type "
                "'{t}' not "
                "compatible with '{t}'",
                (FormatArg[]){{.t = node->type}, {.t = ctx->lastReturn->type}});
        }
    }
    ctx->lastReturn = node;
}

static void checkDeferStmt(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    if (node->parentScope == NULL || node->parentScope->tag != astBlockStmt) {
        logError(ctx->L,
                 &node->loc,
                 "use of 'defer' statement outside of a block",
                 NULL);
    }

    node->type = evalType(visitor, node->deferStmt.expr);
}

static void checkBreakContinueStmt(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    findEnclosingLoop(&ctx->env,
                      ctx->L,
                      node->tag == astBreakStmt ? "break" : "continue",
                      &node->loc);
    node->type = makeVoidType(ctx->typeTable);
}

static void checkIfStmt(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    pushScope(&ctx->env, node);

    const Type *cond = evalType(visitor, node->ifStmt.cond);
    const Type *then = evalType(visitor, node->ifStmt.body);

    if (!isTypeAssignableFrom(getPrimitiveType(ctx->typeTable, prtBool),
                              cond)) {
        logError(ctx->L,
                 &node->ternaryExpr.cond->loc,
                 "unexpected type in if statement condition, expecting "
                 "a truthy expression but got '{t}'",
                 (FormatArg[]){{.t = cond}});
        node->type = ERROR_TYPE(ctx);
    }
    else {
        node->type = then;
    }

    if (node->ifStmt.otherwise) {
        evalType(visitor, node->ifStmt.otherwise);
    }

    popScope(&ctx->env);
}

static void checkWhileStmt(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    pushScope(&ctx->env, node);
    const Type *cond = evalType(visitor, node->whileStmt.cond);
    const Type *body = evalType(visitor, node->whileStmt.body);

    if (!isTypeAssignableFrom(getPrimitiveType(ctx->typeTable, prtBool),
                              cond)) {
        logError(ctx->L,
                 &node->ternaryExpr.cond->loc,
                 "unexpected type in while statement condition, expecting "
                 "a truthy expression, but got '{t}'",
                 (FormatArg[]){{.t = cond}});
        node->type = ERROR_TYPE(ctx);
    }
    else {
        node->type = body;
    }
    popScope(&ctx->env);
}

static void checkPrimitiveType(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    node->type = getPrimitiveType(ctx->typeTable, node->primitiveType.id);
}

static void checkPointerType(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    node->type = makePointerType(ctx->typeTable,
                                 evalType(visitor, node->pointerType.pointed),
                                 node->flags & flgConst);
}

static void checkArrayType(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *element = evalType(visitor, node->arrayType.elementType);

    u64 size = UINT64_MAX;
    if (node->arrayType.dim) {
        // TODO evaluate size
        evalType(visitor, node->arrayType.dim);
        csAssert0(node->arrayType.dim->tag == astIntegerLit);
        size = node->arrayType.dim->intLiteral.value;
    }

    node->type = makeArrayType(ctx->typeTable, element, size);
}

static void checkTypeDecl(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    defineSymbol(&ctx->env, ctx->L, node->typeDecl.name, node);
    if (node->typeDecl.aliased) {
        const Type *ref = evalType(visitor, node->typeDecl.aliased);
        node->type = makeAliasType(ctx->typeTable, ref, node->typeDecl.name);
    }
    else {
        node->type = makeOpaqueType(ctx->typeTable, node->typeDecl.name);
    }
}

static void checkUnionDecl(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    defineSymbol(&ctx->env, ctx->L, node->unionDecl.name, node);

    u64 count = countAstNodes(node->unionDecl.members);
    const Type **members = mallocOrDie(sizeof(Type *) * count);

    AstNode *member = node->unionDecl.members;
    for (u64 i = 0; member; member = member->next, i++) {
        members[i] = evalType(visitor, member);
        if (members[i] == ERROR_TYPE(ctx))
            node->type = ERROR_TYPE(ctx);
    }

    if (node->type == NULL)
        node->type = makeUnionType(ctx->typeTable, members, count);

    free((void *)members);
}

static void checkTupleType(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    u64 count = countAstNodes(node->tupleType.args);
    const Type **args = mallocOrDie(sizeof(Type *) * count);

    AstNode *arg = node->tupleType.args;
    for (u64 i = 0; arg; arg = arg->next, i++) {
        args[i] = evalType(visitor, arg);
        if (args[i] == ERROR_TYPE(ctx))
            node->type = ERROR_TYPE(ctx);
    }

    if (node->type == NULL)
        node->type = makeTupleType(ctx->typeTable, args, count, flgNone);

    free(args);
}

static void checkFuncType(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    const Type *ret = evalType(visitor, node->funcType.ret);
    u64 count = countAstNodes(node->funcType.params);
    const Type **params = mallocOrDie(sizeof(Type *) * count);

    AstNode *param = node->funcType.params;
    for (u64 i = 0; param; param = param->next, i++) {
        param->parentScope = node;
        params[i] = evalType(visitor, param);
        if (params[i] == ERROR_TYPE(ctx))
            node->type = ERROR_TYPE(ctx);
    }

    if (node->type == NULL)
        node->type = makeFuncType(ctx->typeTable,
                                  &(Type){.tag = typFunc,
                                          .name = NULL,
                                          .flags = node->flags,
                                          .func = {.retType = ret,
                                                   .params = params,
                                                   .paramsCount = count,
                                                   .decl = node}});

    free(params);
}

static void checkBuiltinType(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    node->type = node->tag == astVoidType ? makeVoidType(ctx->typeTable)
                                          : makeStringType(ctx->typeTable);
}

static void checkOptionalType(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *type = evalType(visitor, node->optionalType.type);
    node->type = makeOptionalType(ctx->typeTable, type, flgNone);
}

static void checkEnumDecl(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    u64 numOptions = countAstNodes(node->enumDecl.options);
    EnumOption *options = mallocOrDie(sizeof(EnumOption) * numOptions);
    AstNode *option = node->enumDecl.options;
    u64 lastValue = 0, i = 0;
    const Type *base = NULL;
    Env env;

    if (node->enumDecl.base)
        base = evalType(visitor, node->enumDecl.base);
    else
        base = getPrimitiveType(ctx->typeTable, prtI64);

    if (!isIntegerType(base)) {
        logError(ctx->L,
                 &node->enumDecl.base->loc,
                 "expecting enum base to be an integral type, got '{t}'",
                 (FormatArg[]){{.t = base}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    defineSymbol(&ctx->env, ctx->L, node->enumDecl.name, node);
    environmentInit(&env);
    environmentAttachUp(&env, &ctx->env);
    pushScope(&env, node);

    for (; option; option = option->next, i++) {
        option->flags |= flgMember;

        if (!defineSymbol(&env, ctx->L, option->enumOption.name, option)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }

        u64 value = option->enumOption.value
                        ? option->enumOption.value->intLiteral.value
                        : lastValue;
        options[i] =
            (EnumOption){.value = value, .name = option->enumOption.name};
        lastValue = value + 1;
        option->enumOption.index = i;
    }

    environmentDetachUp(&env);

    node->type = makeEnum(ctx->typeTable,
                          &(Type){.tag = typEnum,
                                  .name = node->enumDecl.name,
                                  .flags = node->flags,
                                  .tEnum = {.base = base,
                                            .options = options,
                                            .count = numOptions,
                                            .env = &env}});

    free(options);
}

static void checkStructField(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *type = node->structField.type
                           ? evalType(visitor, node->structField.type)
                           : makeAutoType(ctx->typeTable);
    if (node->structField.value) {
        const Type *value = evalType(visitor, node->structField.value);
        if (!isTypeAssignableFrom(type, value)) {
            logError(ctx->L,
                     &node->structField.value->loc,
                     "field initializer of type '{t}' not compatible with "
                     "field type '{t}'",
                     (FormatArg[]){{.t = value}, {.t = type}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
        type = value;
    }

    node->type = type;
    defineSymbol(&ctx->env, ctx->L, node->structField.name, node);
}

static void checkStructDecl(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    u64 numMembers = countAstNodes(node->structDecl.members);
    StructField *members = mallocOrDie(sizeof(StructField) * numMembers);
    AstNode *member = node->structDecl.members;

    const Type *base = NULL;
    if (node->structDecl.base) {
        base = evalType(visitor, node->structDecl.base);
        if (base->tag != typStruct) {
            logError(ctx->L,
                     &node->structDecl.base->loc,
                     "type '{t}' cannot be extend",
                     (FormatArg[]){{.t = base}});
            node->type = ERROR_TYPE(ctx);
            goto checkStructDecl_error;
        }
    }

    node->type =
        makeThisType(ctx->typeTable, node->structDecl.name, node->flags);
    defineSymbol(&ctx->env, ctx->L, node->structDecl.name, node);

    Env env = ctx->env;
    environmentInit(&ctx->env);
    pushScope(&ctx->env, node);
    if (base) {
        environmentAttachUp((Env *)base->tStruct.env, &env);
        environmentAttachUp(&ctx->env, (Env *)base->tStruct.env);
    }
    else {
        environmentAttachUp(&ctx->env, &env);
    }

    u64 i = 0;
    for (; member; member = member->next, i++) {
        member->parentScope = node;
        const Type *type;
        if (member->tag == astFuncDecl) {
            type = checkMethodDeclSignature(visitor, member);
        }
        else {
            type = evalType(visitor, member);
        }

        if (type == ERROR_TYPE(ctx)) {
            node->type = ERROR_TYPE(ctx);
            goto checkStructDecl_cleanupScopes;
        }

        if (member->tag == astFuncDecl) {
            members[i] = (StructField){
                .name = member->funcDecl.name, .type = type, .decl = member};
        }
        else {
            members[i] = (StructField){
                .name = member->structField.name, .type = type, .decl = member};
            member->structField.index = i;
        }
    }

    if (base) {
        environmentDetachUp((Env *)base->tStruct.env);
        environmentDetachUp(&ctx->env);
    }
    else {
        environmentDetachUp(&ctx->env);
    }

    Env structEnv = {NULL};
    releaseScope(&ctx->env, &structEnv);

    node->type = makeStruct(ctx->typeTable,
                            &(Type){.tag = typStruct,
                                    .flags = node->flags,
                                    .name = node->structDecl.name,
                                    .tStruct = {.env = &structEnv,
                                                .base = base,
                                                .fields = members,
                                                .fieldsCount = i}});
    environmentFree(&ctx->env);
    ctx->env = structEnv;

    if (base) {
        environmentAttachUp((Env *)base->tStruct.env, &env);
        environmentAttachUp(&ctx->env, (Env *)base->tStruct.env);
    }
    else {
        environmentAttachUp(&ctx->env, &env);
    }

    member = node->structDecl.members;
    AstNode *prev = member;
    for (; member; member = member->next) {
        if (member->tag == astFuncDecl) {
            checkMethodDeclBody(visitor, member);
            if (member == node->structDecl.members) {
                node->structDecl.members = member->next;
            }
            else {
                prev->next = member->next;
            }

            member->next = NULL;
            addTopLevelDecl(ctx, NULL, member);
            member = prev;
        }
        else {
            prev = member;
        }
    }

    if (ctx->env.scope->next) {
        Env tmp = {.first = ctx->env.first->next};
        ctx->env.first->next = NULL;
        ctx->env = tmp;
    }

    if (base) {
        environmentDetachUp((Env *)base->tStruct.env);
        environmentDetachUp(&ctx->env);
    }
    else {
        environmentDetachUp(&ctx->env);
    }

checkStructDecl_cleanupScopes:
    environmentFree(&ctx->env);
    ctx->env = env;

checkStructDecl_error:
    free(members);
}

void semanticsCheck(AstNode *program,
                    Log *L,
                    MemPool *pool,
                    StrPool *strPool,
                    TypeTable *typeTable)
{
    SemanticsContext context = {.L = L,
                                .typeTable = typeTable,
                                .pool = pool,
                                .strPool = strPool,
                                .env = {NULL}};
    environmentInit(&context.env);

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context,
    {
        [astProgram] = checkProgram,
        [astPathElem] = checkPathElement,
        [astPath] = checkPath,
        [astNullLit] = checkLiterals,
        [astBoolLit] = checkLiterals,
        [astCharLit] = checkLiterals,
        [astIntegerLit] = checkLiterals,
        [astFloatLit] = checkLiterals,
        [astStringLit] = checkLiterals,
        [astStringExpr] = checkStringExpr,
        [astFuncParam] = checkFuncParam,
        [astFuncDecl] = checkFunctionDecl,
        [astVarDecl] = checkVarDecl,
        [astIdentifier] = checkIdentifier,
        [astBinaryExpr] = checkBinary,
        [astAssignExpr] = checkAssign,
        [astUnaryExpr] = checkUnary,
        [astAddressOf] = checkAddressOf,
        [astIndexExpr] = checkIndex,
        [astMemberExpr] = checkMember,
        [astArrayExpr] = checkArrayExr,
        [astTupleExpr] = checkTupleExpr,
        [astStructExpr] = checkStructExpr,
        [astGroupExpr] = checkGroupExpr,
        [astCallExpr] = checkCall,
        [astClosureExpr] = checkClosure,
        [astCastExpr] = checkCastExpr,
        [astRangeExpr] = checkRangeExpr,
        [astTernaryExpr] = checkTernaryExpr,
        [astNewExpr] = checkNewExpr,
        [astBlockStmt] = checkBlock,
        [astReturnStmt] = checkReturn,
        [astDeferStmt] = checkDeferStmt,
        [astBreakStmt] = checkBreakContinueStmt,
        [astContinueStmt] = checkBreakContinueStmt,
        [astIfStmt] = checkIfStmt,
        [astWhileStmt] = checkWhileStmt,
        [astForStmt] = checkForStmt,
        [astPrimitiveType] = checkPrimitiveType,
        [astArrayType] = checkArrayType,
        [astPointerType] = checkPointerType,
        [astTypeDecl] = checkTypeDecl,
        [astUnionDecl] = checkUnionDecl,
        [astTupleType] = checkTupleType,
        [astFuncType] = checkFuncType,
        [astVoidType] = checkBuiltinType,
        [astStringType] = checkBuiltinType,
        [astOptionalType] = checkOptionalType,
        [astEnumDecl] = checkEnumDecl,
        [astStructDecl] = checkStructDecl,
        [astStructField] = checkStructField
    },
    .fallback = checkFallback);
    // clang-format off

    astVisit(&visitor, program);
}
