//
// Created by Carter on 2023-03-28.
//

#include "check.h"
#include "capture.h"
#include "scope.h"
#include "ttable.h"

#include "core/alloc.h"
#include "core/utils.h"

#include <string.h>

typedef struct {
    Log *L;
    MemPool *pool;
    StrPool *strPool;
    TypeTable *typeTable;
    Env env;
    Scope *closure;
    AstNode *previousTopLevelDecl;
    AstNode *currentTopLevelDecl;
    AstNode *program;
    const AstNode *lastReturn;
    bool mainOptimized;
    u64 anonymousDeclsIndex;
} CheckerContext;

static const Type *sError;

static inline const Type *evalType(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    astVisit(visitor, node);
    return resolveType(ctx->typeTable, node->type);
}

static inline u64 checkMany(AstVisitor *visitor, AstNode *node)
{
    u64 i = 0;
    for (; node; node = node->next, i++)
        astVisit(visitor, node);

    return i;
}

static void addBuiltinFunc(CheckerContext *ctx,
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

static void addBuiltinVariable(CheckerContext *ctx,
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

static void addBuiltinType(CheckerContext *ctx,
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

static void initBuiltins(CheckerContext *ctx)
{
    {
        const Type *params[] = {makePrimitiveType(ctx->typeTable, prtChar)};
        addBuiltinFunc(
            ctx, "wputc", makePrimitiveType(ctx->typeTable, prtI32), params, 1);
    }

    {
        addBuiltinType(
            ctx, "char", flgNative, makeOpaqueType(ctx->typeTable, "char"));

        addBuiltinType(ctx,
                       "__cxy_range_t",
                       flgNative,
                       makeOpaqueType(ctx->typeTable, "__cxy_range_t"));
    }
}

static void addAnonymousTopLevelDecl(CheckerContext *ctx,
                                     cstring name,
                                     AstNode *node)
{
    Env env = {.first = ctx->env.first, .scope = ctx->env.first};
    if (!defineSymbol(&env, ctx->L, name, node))
        return;
    if (ctx->previousTopLevelDecl == ctx->currentTopLevelDecl) {
        ctx->program->program.decls = node;
        node->next = ctx->currentTopLevelDecl;
    }
    else {
        ctx->previousTopLevelDecl->next = node;
        node->next = ctx->currentTopLevelDecl;
    }
    ctx->previousTopLevelDecl = node;
}

static void checkProgram(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
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
    CheckerContext *ctx = getAstVisitorContext(visitor);

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

static void checkLiterals(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);

    switch (node->tag) {
    case astNullLit:
        node->type = makeNullType(ctx->typeTable);
        break;
    case astBoolLit:
        node->type = makePrimitiveType(ctx->typeTable, prtBool);
        break;
    case astCharLit:
        node->type = makePrimitiveType(ctx->typeTable, prtChar);
        break;
    case astIntegerLit:
        node->type = makePrimitiveType(ctx->typeTable, prtI32);
        break;
    case astFloatLit:
        node->type = makePrimitiveType(ctx->typeTable, prtF32);
        break;
    case astStringLit:
        node->type = makeStringType(ctx->typeTable);
        break;
    default:
        csAssert0("Not a literal");
    }
}

static void checkStringExpr(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    checkMany(visitor, node->stringExpr.parts);
    AstNode *part = node->stringExpr.parts;
    for (; part; part = part->next) {
        part->type = evalType(visitor, part);
        if (part->type == sError) {
            node->type = sError;
        }
    }

    node->type = makeStringType(ctx->typeTable);
}

static void checkFuncParam(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    if (node->parentScope == NULL || node->parentScope->tag != astFuncType)
        defineSymbol(&ctx->env, ctx->L, node->funcParam.name, node);

    if (node->funcParam.type)
        node->type = evalType(visitor, node->funcParam.type);
    else
        csAssert0(node->type);

    if (node->funcParam.def) {
        const Type *def = evalType(visitor, node->funcParam.def);
        if (!isTypeAssignableFrom(ctx->typeTable, node->type, def)) {
            logError(ctx->L,
                     &node->funcParam.def->loc,
                     "parameter default value type '{t}' not compatible with "
                     "parameter type '{t}",
                     (FormatArg[]){{.t = def}, {.t = node->type}});
        }
    }
}

static const Type *transformFuncTypeParam(CheckerContext *ctx, const Type *type)
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

static void checkFunctionDecl(AstVisitor *visitor, AstNode *node)
{
    const Type *ret = NULL, **params, *type = NULL;
    CheckerContext *ctx = getAstVisitorContext(visitor);
    const AstNode *lastReturn = ctx->lastReturn;
    ctx->lastReturn = NULL;

    u64 paramsCount = countAstNodes(node->funcDecl.params);
    AstNode *param = node->funcDecl.params;
    u64 i = 0;
    bool isVariadic = false;
    u64 withDefaultValues = 0;

    defineSymbol(&ctx->env, ctx->L, node->funcDecl.name, node);
    if (!ctx->mainOptimized) {
        node->flags |=
            (strcmp(node->funcDecl.name, "main") == 0) ? flgMain : flgNone;
        ctx->mainOptimized = node->flags & flgMain;
    }

    pushScope(&ctx->env, node);
    params = mallocOrDie(sizeof(Type *) * paramsCount);

    for (; param; param = param->next, i++) {
        param->parentScope = node;
        params[i] = evalType(visitor, param);
        if (isVariadic && (param->flags & flgVariadic)) {
            logError(ctx->L,
                     &param->loc,
                     "variadic parameters should the last parameter type in "
                     "function declaration",
                     NULL);
            continue;
        }

        isVariadic = (param->flags & flgVariadic);

        if (withDefaultValues && param->funcParam.def == NULL) {
            logError(ctx->L,
                     &param->loc,
                     "parameter cannot be declared without a default value",
                     NULL);
            continue;
        }
        withDefaultValues = (param->funcParam.def != NULL);
        if (params[i]->tag == typFunc) {
            params[i] = transformFuncTypeParam(ctx, params[i]);
            param->type = params[i];

            param->flags |= flgFuncTypeParam;
            node->flags |= flgClosureStyle;
        }
    }

    ret = makeAutoType(ctx->typeTable);
    if (node->funcDecl.ret)
        ret = evalType(visitor, node->funcDecl.ret);

    node->type = makeFuncType(
        ctx->typeTable,
        &(Type){.tag = typFunc,
                .name = node->funcDecl.name,
                .flags = node->flags,
                .func = {.retType = ret,
                         .params = params,
                         .paramsCount = paramsCount,
                         .decl = node,
                         .defaultValuesCount = withDefaultValues}});

    if (!(node->flags & flgNative)) {
        node->funcDecl.body->parentScope = node;
        ret = evalType(visitor, node->funcDecl.body);
    }
    if (ctx->lastReturn && ret == makeVoidType(ctx->typeTable))
        ret = ctx->lastReturn->type;

    ((Type *)(node->type))->func.retType = ret;

    ctx->lastReturn = lastReturn;

    free((void *)params);
    popScope(&ctx->env);
}

static void checkClosure(AstVisitor *visitor, AstNode *node)
{
    const Type *ret, **params;
    CheckerContext *ctx = getAstVisitorContext(visitor);
    const AstNode *lastReturn = ctx->lastReturn;
    ctx->lastReturn = NULL;

    u64 paramsCount = countAstNodes(node->closureExpr.params) + 1;
    AstNode *param = node->closureExpr.params;
    u64 i = 1;

    pushScope(&ctx->env, node);

    Scope *stack = ctx->closure;
    ctx->closure = ctx->env.scope;

    params = mallocOrDie(sizeof(Type *) * paramsCount);
    for (; param; param = param->next, i++) {
        param->parentScope = node;
        params[i] = evalType(visitor, param);
        if (param->flags & flgVariadic) {
            logError(ctx->L,
                     &param->loc,
                     "variadic parameters are not supported on closures",
                     NULL);
        }
    }

    if (node->closureExpr.ret)
        evalType(visitor, node->closureExpr.ret);

    node->closureExpr.body->parentScope = node;
    ret = evalType(visitor, node->closureExpr.body);

    ctx->closure = stack;

    popScope(&ctx->env);

    // We need to create a tuple for the capture
    u64 index = node->closureExpr.capture.index;
    const Type **capturedTypes = mallocOrDie(sizeof(Type *) * index);
    const char **names = allocFromMemPool(ctx->pool, sizeof(void *) * index);
    index = getOrderedCapture(
        &node->closureExpr.capture, capturedTypes, names, index);
    params[0] = makePointerType(
        ctx->typeTable,
        makeTupleType(ctx->typeTable, capturedTypes, index, flgNone),
        flgNone);
    free((void *)capturedTypes);

    cstring name = makeAnonymousVariable(ctx->strPool, "__cxy_closure_expr");
    node->type = makeFuncType(
        ctx->typeTable,
        &(Type){
            .tag = typFunc,
            .name = NULL,
            .flags = node->flags | flgClosure,
            .func = {.retType = ret,
                     .params = params,
                     .captureNames = names,
                     .capturedNamesCount = index,
                     .paramsCount = paramsCount,
                     .decl = makeAstNode(ctx->pool,
                                         &node->loc,
                                         &(AstNode){.tag = astIdentifier,
                                                    .ident.value = name})}});

    ctx->lastReturn = lastReturn;

    // We need to copy the closure node to global scope and replace it with
    // an identifier.
    AstNode *copy = copyAstNode(ctx->pool, node);
    __typeof(node->closureExpr) closureExpr = node->closureExpr;
    memset(&copy->closureExpr, 0, sizeof(closureExpr));
    copy->tag = astFuncDecl;
    copy->funcDecl.ret = closureExpr.ret;
    copy->funcDecl.params = makeAstNode(ctx->pool,
                                        &copy->loc,
                                        &(AstNode){.tag = astFuncParam,
                                                   .type = params[0],
                                                   .flags = flgCapture,
                                                   .next = closureExpr.params,
                                                   .funcParam = {
                                                       .name = "self",
                                                   }});

    copy->funcDecl.body = closureExpr.body;
    copy->funcDecl.name = name;
    copy->flags |= flgClosure;

    addAnonymousTopLevelDecl(ctx, copy->funcDecl.name, copy);

    node->next = NULL;
    node->tag = astPath;
    node->path.elements =
        makeAstNode(ctx->pool,
                    &node->loc,
                    &(AstNode){.tag = astPathElem,
                               .type = node->type,
                               .pathElement = {.name = copy->funcDecl.name}});

    free((void *)params);
}

static void checkCastExpr(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    const Type *expr = evalType(visitor, node->castExpr.expr);
    const Type *target = evalType(visitor, node->castExpr.to);
    if (!isTypeCastAssignable(ctx->typeTable, target, expr)) {
        logError(ctx->L,
                 &node->loc,
                 "type '{t}' cannot be cast to type '{t}'",
                 (FormatArg[]){{.t = expr}, {.t = target}});
    }
    node->type = target;
}

static void checkRangeExpr(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    const Type *i64Type = makePrimitiveType(ctx->typeTable, prtI64);

    const Type *start = evalType(visitor, node->rangeExpr.start);
    const Type *end = evalType(visitor, node->rangeExpr.end);

    if (!isTypeAssignableFrom(ctx->typeTable, i64Type, start)) {
        logError(ctx->L,
                 &node->rangeExpr.start->loc,
                 "expecting integer type for range start expression, got '{t}'",
                 (FormatArg[]){{.t = start}});
        start = sError;
    }

    if (!isTypeAssignableFrom(ctx->typeTable, i64Type, end)) {
        logError(ctx->L,
                 &node->rangeExpr.start->loc,
                 "expecting integer type for range end expression, got '{t}'",
                 (FormatArg[]){{.t = end}});
        start = sError;
    }

    if (node->rangeExpr.step) {
        const Type *step = evalType(visitor, node->rangeExpr.step);
        if (!isTypeAssignableFrom(ctx->typeTable, i64Type, end)) {
            logError(
                ctx->L,
                &node->rangeExpr.start->loc,
                "expecting integer type for range step expression, got '{t}'",
                (FormatArg[]){{.t = step}});
            start = sError;
        }
    }

    node->type = makeOpaqueType(ctx->typeTable, "__cxy_range_t");
}

static void checkTernaryExpr(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    const Type *cond = evalType(visitor, node->ternaryExpr.cond);
    const Type *body = evalType(visitor, node->ternaryExpr.body);
    const Type *otherwise = evalType(visitor, node->ternaryExpr.otherwise);

    if (!isTypeAssignableFrom(
            ctx->typeTable, makePrimitiveType(ctx->typeTable, prtBool), cond)) {
        logError(ctx->L,
                 &node->ternaryExpr.cond->loc,
                 "expecting a ternary expression ('?') condition type of bool, "
                 "got '{t}'",
                 (FormatArg[]){{.t = cond}});
        node->type = sError;
    }
    if (!isTypeAssignableFrom(ctx->typeTable, body, otherwise)) {
        logError(ctx->L,
                 &node->loc,
                 "operands to ternary expression ('?') have different types, "
                 "'{t}' and '{t}'",
                 (FormatArg[]){{.t = body}, {.t = otherwise}});
        node->type = sError;
    }
    else
        node->type = body;
}

static void checkNewExpr(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    const Type *type = NULL, *init = NULL;
    type = evalType(visitor, node->newExpr.type);
    if (node->newExpr.init)
        init = evalType(visitor, node->newExpr.init);

    if (init && !isTypeAssignableFrom(ctx->typeTable, type, init)) {
        logError(
            ctx->L,
            &node->loc,
            "new initializer value type '{t}' is not assignable to type '{t}'",
            (FormatArg[]){{.t = type}, {.t = init}});
    }
    node->flags = node->newExpr.type->flags;
    node->type = makePointerType(ctx->typeTable, type, type->flags);
}

static void checkVarDecl(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
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
            node->type = sError;
        }
        else if (!isTypeAssignableFrom(ctx->typeTable, node->type, value)) {
            logError(ctx->L,
                     &node->varDecl.init->loc,
                     "incompatible types, expecting type '{t}', got '{t}'",
                     (FormatArg[]){{.t = node->type}, {.t = value}});
            node->type = sError;
        }
        else if ((value->tag == typPointer) &&
                 ((value->flags & flgConst) && !(node->flags & flgConst))) {
            logError(ctx->L,
                     &node->varDecl.init->loc,
                     "assigning a const pointer to a non-const variable "
                     "discards const qualifier",
                     NULL);
            node->type = sError;
        }
        if (node->type->tag == typAuto)
            node->type = value;
    }
}

static void checkIdentifier(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    AstNode *symbol =
        findSymbol(&ctx->env, ctx->L, node->pathElement.name, &node->loc);
    if (symbol == NULL)
        node->type = sError;
    else
        node->type = symbol->type;
}

static void checkPathElement(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    AstNode *symbol =
        findSymbol(&ctx->env, ctx->L, node->pathElement.name, &node->loc);
    if (symbol == NULL)
        node->type = sError;
    else
        node->type = symbol->type;
}

static const Type *checkFirstPathElement(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);

    Scope *scope = NULL, *closure = ctx->closure;
    AstNode *symbol = findSymbolAndScope(
        &ctx->env, ctx->L, node->pathElement.name, &node->loc, &scope);
    u64 flags = flgNone;
    if (symbol == NULL)
        node->type = sError;
    else {
        node->type = symbol->type;
        flags = (symbol->flags & flgConst);
    }
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
    CheckerContext *ctx = getAstVisitorContext(visitor);
    AstNode *elem = node->path.elements;
    const Type *type = checkFirstPathElement(visitor, elem);
    u64 flags = elem->flags;

    elem = elem->next;
    for (; elem; elem = elem->next) {
        type = evalType(visitor, elem);
        flags = elem->flags;
    }
    node->type = type;
    node->flags |= flags;
}

static void checkBinary(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
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
    const Type *type = promoteType(ctx->typeTable, left, right);
    node->type = sError;

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
        if (!isNumericType(ctx->typeTable, type)) {
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
        if (!isIntegerType(ctx->typeTable, type)) {
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
        if (type != makePrimitiveType(ctx->typeTable, prtBool)) {
            logError(
                ctx->L,
                &node->loc,
                "cannot perform logical binary operation '{s}' on non-boolean "
                "type '{t}'",
                (FormatArg[]){{.s = getBinaryOpString(op)}, {.t = type}});
            return;
        }
        node->type = type;
        break;
    case optComparison:
        if (!isNumericType(ctx->typeTable, type)) {
            logError(ctx->L,
                     &node->loc,
                     "cannot perform comparison binary operation '{s}' on "
                     "non-numeric "
                     "type '{t}'",
                     (FormatArg[]){{.s = getBinaryOpString(op)}, {.t = type}});
            return;
        }
        node->type = makePrimitiveType(ctx->typeTable, prtBool);
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
        node->type = makePrimitiveType(ctx->typeTable, prtBool);
        break;
    case optRange: {
        if (!isIntegerType(ctx->typeTable, left)) {
            logError(
                ctx->L,
                &node->loc,
                "expecting an integral type for range expression start, got "
                "type '{t}'",
                (FormatArg[]){{.t = left}});
            return;
        }
        if (!isIntegerType(ctx->typeTable, right)) {
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
        node->type = makeOpaqueType(ctx->typeTable, "__cxy_range_t");
        break;
    }
    default:
        unreachable("");
    }
}

static const Type *checkPrefixExpr(CheckerContext *ctx,
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
            operand = sError;
            break;
        }
    case opMinus:
    case opPlus:
        if (!isNumericType(ctx->typeTable, operand)) {
            logError(ctx->L,
                     &node->unaryExpr.operand->loc,
                     "postfix expression '{s}' no supported on type '{t}'",
                     (FormatArg[]){{.s = getUnaryOpString(node->unaryExpr.op)},
                                   {.t = operand}});
            operand = sError;
        }
        break;
    case opNot:
        if (operand == makePrimitiveType(ctx->typeTable, prtBool)) {
            logError(ctx->L,
                     &node->unaryExpr.operand->loc,
                     "logical '!' operator no supported on type '{t}', "
                     "expecting bool type",
                     (FormatArg[]){{.t = operand}});
            operand = sError;
        }
        break;
    case opDeref:
        if (operand->tag != typPointer) {
            logError(ctx->L,
                     &node->unaryExpr.operand->loc,
                     "cannot not dereference an non-pointer type '{t}'",
                     (FormatArg[]){{.t = operand}});
            operand = sError;
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
            operand = sError;
        }
        else {
            operand = makeVoidType(ctx->typeTable);
        }
        break;
    default:
        operand = sError;
        break;
    }

    return operand;
}

static void checkUnary(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
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
            operand = sError;
        }
        else if (!isNumericType(ctx->typeTable, operand)) {
            logError(ctx->L,
                     &node->unaryExpr.operand->loc,
                     "postfix expression '{s}' no supported on type '{t}'",
                     (FormatArg[]){{.s = getUnaryOpString(node->unaryExpr.op)},
                                   {.t = operand}});
            operand = sError;
        }
    }
    node->type = operand;
}

static void checkAssign(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    AstNode *left = node->assignExpr.lhs, *right = node->assignExpr.rhs;
    const Type *lhs = evalType(visitor, left);
    const Type *rhs = evalType(visitor, right);
    bool isLeftAuto = lhs == makeAutoType(ctx->typeTable);

    // TODO check r-value-ness
    node->type = sError;
    if ((left->flags & flgConst) || (lhs->flags & flgConst)) {
        logError(ctx->L,
                 &node->loc,
                 "lhs of assignment expressions is a constant",
                 (FormatArg[]){{.t = lhs}});
        node->type = sError;
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
    else if (!isTypeAssignableFrom(ctx->typeTable, lhs, rhs)) {
        logError(ctx->L,
                 &node->assignExpr.rhs->loc,
                 "incompatible types on assigment expression, expecting '{t}', "
                 "got '{t}'",
                 (FormatArg[]){{.t = lhs}, {.t = rhs}});
    }
    if (node->type == sError)
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
    CheckerContext *ctx = getAstVisitorContext(visitor);
    const Type *operand = evalType(visitor, node->unaryExpr.operand);
    node->flags |= node->unaryExpr.operand->flags;
    node->type = makePointerType(
        ctx->typeTable, operand, node->unaryExpr.operand->flags);
}

static void checkIndex(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    const Type *target = evalType(visitor, node->indexExpr.target);
    node->flags |= node->indexExpr.target->flags;

    astVisit(visitor, node->indexExpr.index);
    if (target->tag == typPointer)
        target = stripPointer(ctx->typeTable, target);

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

static void checkMember(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    const Type *target = evalType(visitor, node->memberExpr.target);
    AstNode *member = node->memberExpr.member;
    node->flags |= (node->memberExpr.target->flags & flgConst);

    if (member->tag == astIntegerLit) {
        u64 flags = target->flags;
        target = stripPointer(ctx->typeTable, target);
        if (target->tag != typTuple) {
            logError(ctx->L,
                     &node->memberExpr.target->loc,
                     "literal member expression cannot be used on type '{t}', "
                     "type is not a tuple",
                     (FormatArg[]){{.t = target}});
            node->type = sError;
            return;
        }

        if (member->intLiteral.value >= target->tuple.count) {
            logError(ctx->L,
                     &member->loc,
                     "literal member '{u64}' out of range, type '{t}' has "
                     "{u64} members",
                     (FormatArg[]){{.u64 = member->intLiteral.value},
                                   {.t = target},
                                   {.u64 = target->tuple.count}});
            node->type = sError;
            return;
        }

        node->type = target->tuple.members[member->intLiteral.value];
        node->flags |= ((flags | node->type->flags) & flgConst);
    }
    else {
        csAssert(member->tag == astIdentifier, "TODO");
        node->type = sError;
    }
}

static void checkArrayExr(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    u64 count = 0;
    const Type *elementType = NULL;
    for (AstNode *elem = node->arrayExpr.elements; elem;
         elem = elem->next, count++) {
        const Type *type = evalType(visitor, elem);
        if (elementType == NULL) {
            elementType = type;
            continue;
        }

        if (!isTypeAssignableFrom(ctx->typeTable, elementType, type)) {
            logError(
                ctx->L,
                &elem->loc,
                "inconsistent array types in array, expecting '{t}', got '{t}'",
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
    CheckerContext *ctx = getAstVisitorContext(visitor);
    u64 count = countAstNodes(node->tupleExpr.args);
    const Type **args = mallocOrDie(sizeof(Type *) * count);
    AstNode *arg = node->tupleExpr.args;

    for (u64 i = 0; arg; arg = arg->next, i++) {
        args[i] = evalType(visitor, arg);
        if (args[i] == sError)
            node->type = sError;
    }

    if (node->type == NULL) {
        node->type = makeTupleType(ctx->typeTable, args, count, flgNone);
    }

    free(args);
}

static void checkGroupExpr(AstVisitor *visitor, AstNode *node)
{
    node->type = evalType(visitor, node->groupExpr.expr);
}

static const Type *transformFuncTypeParamCallee(CheckerContext *ctx,
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
    CheckerContext *ctx = getAstVisitorContext(visitor);
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
    CheckerContext *ctx = getAstVisitorContext(visitor);
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
        node->type = sError;
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
        if (!isTypeAssignableFrom(ctx->typeTable, expected, type)) {
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
    CheckerContext *ctx = getAstVisitorContext(visitor);
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
    CheckerContext *ctx = getAstVisitorContext(visitor);
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

    if (ret && !isTypeAssignableFrom(ctx->typeTable, ret, node->type)) {
        logError(ctx->L,
                 &node->returnStmt.expr->loc,
                 "return value of type '{t}' incompatible with function return "
                 "type '{t}",
                 (FormatArg[]){{.t = node->type}, {.t = ret}});
    }
    else if (ctx->lastReturn) {
        // we have already seen a return
        if (!isTypeAssignableFrom(
                ctx->typeTable, ctx->lastReturn->type, node->type)) {
            logError(
                ctx->L,
                &node->returnStmt.expr->loc,
                "inconsistent return types in auto function, type '{t}' not "
                "compatible with '{t}'",
                (FormatArg[]){{.t = node->type}, {.t = ctx->lastReturn->type}});
        }
    }
    ctx->lastReturn = node;
}

static void checkDeferStmt(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);

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
    CheckerContext *ctx = getAstVisitorContext(visitor);
    findEnclosingLoop(&ctx->env,
                      ctx->L,
                      node->tag == astBreakStmt ? "break" : "continue",
                      &node->loc);
    node->type = makeVoidType(ctx->typeTable);
}

static void checkIfStmt(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    pushScope(&ctx->env, node);

    const Type *cond = evalType(visitor, node->ifStmt.cond);
    const Type *then = evalType(visitor, node->ifStmt.body);

    if (!isTypeAssignableFrom(
            ctx->typeTable, makePrimitiveType(ctx->typeTable, prtBool), cond)) {
        logError(ctx->L,
                 &node->ternaryExpr.cond->loc,
                 "unexpected type in if statement condition, expecting "
                 "a truthy expression but got '{t}'",
                 (FormatArg[]){{.t = cond}});
        node->type = sError;
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
    CheckerContext *ctx = getAstVisitorContext(visitor);
    pushScope(&ctx->env, node);
    const Type *cond = evalType(visitor, node->whileStmt.cond);
    const Type *body = evalType(visitor, node->whileStmt.body);

    if (!isTypeAssignableFrom(
            ctx->typeTable, makePrimitiveType(ctx->typeTable, prtBool), cond)) {
        logError(ctx->L,
                 &node->ternaryExpr.cond->loc,
                 "unexpected type in while statement condition, expecting "
                 "a truthy expression, but got '{t}'",
                 (FormatArg[]){{.t = cond}});
        node->type = sError;
    }
    else {
        node->type = body;
    }
    popScope(&ctx->env);
}

static void checkForStmtGenerator(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    AstNode orig = *node;

    AstNode *range = orig.forStmt.range;
    AstNode *args = getLastAstNode(range->callExpr.args), *arg = NULL,
            *names = orig.forStmt.var->varDecl.names, *closureArgs = NULL;
    u64 varCount = countAstNodes(names), i = 1;

    const Type *callee = evalType(visitor, range->callExpr.callee),
               *bodyFunc = NULL;

    if (callee->tag != typFunc || callee->func.paramsCount == 0 ||
        callee->func.params[callee->func.paramsCount - 1]->flags !=
            flgFuncTypeParam) {
        logError(ctx->L,
                 &range->callExpr.callee->loc,
                 "for range expression is not a generator functions",
                 NULL);
        node->type = sError;
        return;
    }

    bodyFunc =
        callee->func.params[callee->func.paramsCount - 1]->tuple.members[1];
    if (varCount != bodyFunc->func.paramsCount - 1) {
        logError(ctx->L,
                 &node->forStmt.var->loc,
                 "for loop variable declaration mismatch, declared {u64}, "
                 "expecting {u64}",
                 (FormatArg[]){{.u64 = varCount},
                               {.u64 = bodyFunc->func.paramsCount}});
        node->type = sError;
        return;
    }

    for (AstNode *name = names; name; name = name->next, i++) {
        AstNode *newArg = makeAstNode(
            ctx->pool,
            &args->loc,
            &(AstNode){.type = bodyFunc->func.params[i],
                       .tag = astFuncParam,
                       .funcParam = {.name = name->ident.value, .type = NULL}});
        if (closureArgs == NULL) {
            closureArgs = newArg;
            arg = newArg;
        }
        else {
            arg->next = newArg;
            arg = newArg;
        }
    }

    args->next =
        makeAstNode(ctx->pool,
                    &args->loc,
                    &(AstNode){.tag = astClosureExpr,
                               .closureExpr = {.params = closureArgs,
                                               .body = node->forStmt.body}});

    memset(&node->forStmt, 0, sizeof(node->forStmt));
    node->tag = astCallExpr;
    node->callExpr = orig.forStmt.range->callExpr;

    evalType(visitor, node);
}

static void checkForStmt(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    if (node->forStmt.range->tag == astCallExpr) {
        checkForStmtGenerator(visitor, node);
        return;
    }

    pushScope(&ctx->env, node);
    const Type *type = evalType(visitor, node->forStmt.var);
    const Type *range = evalType(visitor, node->forStmt.range);

    AstNode *symbol = findSymbol(&ctx->env,
                                 ctx->L,
                                 node->forStmt.var->varDecl.names->ident.value,
                                 &node->loc);
    csAssert0(symbol);
    if (node->forStmt.range->tag == astRangeExpr) {
        if (type->tag != typAuto && !isIntegerType(ctx->typeTable, type)) {
            logError(ctx->L,
                     &node->forStmt.var->loc,
                     "unexpected type for loop variable type '{t}', expecting "
                     "an integral type",
                     (FormatArg[]){{.t = type}});
            type = sError;
        }
        else if (type->tag == typAuto) {
            symbol->type = makePrimitiveType(ctx->typeTable, prtI64);
            node->forStmt.var->type = symbol->type;
        }
    }
    else if (stripPointer(ctx->typeTable, range)->tag == typArray) {
        const Type *elementType =
            stripPointer(ctx->typeTable, range)->array.elementType;
        if (type->tag != typAuto &&
            !isTypeAssignableFrom(ctx->typeTable, elementType, type)) {
            logError(ctx->L,
                     &node->forStmt.var->loc,
                     "unexpected type '{t}' for loop variable, expecting array "
                     "element type '{t}'",
                     (FormatArg[]){{.t = type}, {.t = elementType}});
            type = sError;
        }
        else if (type->tag == typAuto) {
            symbol->type = elementType;
            node->forStmt.var->type = elementType;
        }
    }
    else if (range->tag == typFunc) {
        unreachable("");
    }

    const Type *body = evalType(visitor, node->forStmt.body);

    node->type = type == sError ? type : body;

    popScope(&ctx->env);
}

static void checkPrimitiveType(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    node->type = makePrimitiveType(ctx->typeTable, node->primitiveType.id);
}

static void checkPointerType(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    node->type = makePointerType(ctx->typeTable,
                                 evalType(visitor, node->pointerType.pointed),
                                 node->flags & flgConst);
}

static void checkArrayType(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
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
    CheckerContext *ctx = getAstVisitorContext(visitor);
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
    CheckerContext *ctx = getAstVisitorContext(visitor);
    defineSymbol(&ctx->env, ctx->L, node->unionDecl.name, node);

    u64 count = countAstNodes(node->unionDecl.members);
    const Type **members = mallocOrDie(sizeof(Type *) * count);

    AstNode *member = node->unionDecl.members;
    for (u64 i = 0; member; member = member->next, i++) {
        members[i] = evalType(visitor, member);
        if (members[i] == sError)
            node->type = sError;
    }

    if (node->type == NULL)
        node->type = makeUnionType(ctx->typeTable, members, count);

    free((void *)members);
}

static void checkTupleType(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);

    u64 count = countAstNodes(node->tupleType.args);
    const Type **args = mallocOrDie(sizeof(Type *) * count);

    AstNode *arg = node->tupleType.args;
    for (u64 i = 0; arg; arg = arg->next, i++) {
        args[i] = evalType(visitor, arg);
        if (args[i] == sError)
            node->type = sError;
    }

    if (node->type == NULL)
        node->type = makeTupleType(ctx->typeTable, args, count, flgNone);

    free(args);
}

static void checkFuncType(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);

    const Type *ret = evalType(visitor, node->funcType.ret);
    u64 count = countAstNodes(node->funcType.params);
    const Type **params = mallocOrDie(sizeof(Type *) * count);

    AstNode *param = node->funcType.params;
    for (u64 i = 0; param; param = param->next, i++) {
        param->parentScope = node;
        params[i] = evalType(visitor, param);
        if (params[i] == sError)
            node->type = sError;
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
    CheckerContext *ctx = getAstVisitorContext(visitor);
    node->type = node->tag == astVoidType ? makeVoidType(ctx->typeTable)
                                          : makeStringType(ctx->typeTable);
}

static void checkOptionalType(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    const Type *type = evalType(visitor, node->optionalType.type);
    node->type = makeOptionalType(ctx->typeTable, type, flgNone);
}

void semanticsCheck(AstNode *program,
                    Log *L,
                    MemPool *pool,
                    StrPool *strPool,
                    TypeTable *typeTable)
{
    CheckerContext context = {.L = L,
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
    },
    .fallback = checkFallback);
    // clang-format off

    sError = makeErrorType(context.typeTable);

    astVisit(&visitor, program);
}
