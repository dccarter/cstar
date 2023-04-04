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

static void addBuiltinType(CheckerContext *ctx, cstring name, const Type *type)
{
    defineSymbol(
        &ctx->env,
        ctx->L,
        name,
        makeAstNode(
            ctx->pool,
            builtinLoc(),
            &(AstNode){.tag = astTypeDecl, .flags = flgBuiltin, .type = type}));
}

static void initBuiltins(CheckerContext *ctx)
{
    {
        const Type *params[] = {makePrimitiveType(ctx->typeTable, prtChar)};
        addBuiltinFunc(
            ctx, "wputc", makePrimitiveType(ctx->typeTable, prtI32), params, 1);
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
    const AstNode *part = node->stringExpr.parts;
    for (; part; part = part->next) {
        if (part->type == sError) {
            node->type = sError;
        }
    }

    node->type = makeStringType(ctx->typeTable);
}

static void checkFuncParam(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    defineSymbol(&ctx->env, ctx->L, node->funcParam.name, node);
    node->type = evalType(visitor, node->funcParam.type);
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
    for (; param; param = param->next) {
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
    params[0] =
        makePointerType(ctx->typeTable,
                        makeTupleType(ctx->typeTable, capturedTypes, index),
                        flgNone);
    free((void *)capturedTypes);
    node->type = makeFuncType(ctx->typeTable,
                              &(Type){.tag = typFunc,
                                      .name = NULL,
                                      .flags = node->flags | flgClosure,
                                      .func = {.retType = ret,
                                               .params = params,
                                               .captureNames = names,
                                               .capturedNamesCount = index,
                                               .paramsCount = paramsCount,
                                               .decl = node}});

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
                                                       .name = "__closure",
                                                   }});

    copy->funcDecl.body = closureExpr.body;
    copy->funcDecl.name =
        makeAnonymousVariable(ctx->strPool, "__cxy_closure_expr");

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

static void checkTypedExpr(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    const Type *expr = evalType(visitor, node->typedExpr.expr);
    const Type *target = evalType(visitor, node->typedExpr.type);
    if (!isTypeCastAssignable(ctx->typeTable, target, expr)) {
        logError(ctx->L,
                 &node->loc,
                 "type '{t}' cannot be cast to type '{t}'",
                 (FormatArg[]){{.t = expr}, {.t = target}});
    }
    node->type = target;
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

static void checkVarDecl(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    AstNode *names = node->varDecl.names;

    defineSymbol(&ctx->env, ctx->L, names->ident.value, node);

    node->type = node->varDecl.type ? evalType(visitor, node->varDecl.type)
                                    : makeAutoType(ctx->typeTable);
    const Type *value = NULL;
    if (node->varDecl.init) {
        value = evalType(visitor, node->varDecl.init);
        if (!isTypeAssignableFrom(ctx->typeTable, node->type, value)) {
            logError(ctx->L,
                     &node->varDecl.init->loc,
                     "incompatible types, expecting type '{t}', got '{t}'",
                     (FormatArg[]){{.t = node->type}, {.t = value}});
        }
        else {
            node->type = value;
        }
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
    if (symbol == NULL)
        node->type = sError;
    else
        node->type = symbol->type;
    node->flags |= (symbol->flags & flgConst);

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
                              (symbol->flags & flgConst) | flgCapturePointer));

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
    } opType = optInvalid;

    Operator op = node->binaryExpr.op;
    const Type *type = promoteType(ctx->typeTable, left, right);
    node->type = sError;

    switch (op) {
        // Numeric arithmetic
#define f(O, ...) case op##O:
        AST_ARITH_EXPR_LIST(f)
        opType = optNumeric;
        break;

        AST_BIT_EXPR_LIST(f)
        AST_SHIFT_EXPR_LIST(f)
        opType = optInteger;
        break;

        AST_LOGIC_EXPR_LIST(f)
        opType = optLogical;
        break;

        AST_CMP_EXPR_LIST(f)
        opType = (op == opEq || op == opNe) ? optEquality : optComparison;
        break;
    case opRange:
        opType = optRange;
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

    switch (opType) {
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
            return;
        }
        node->type = makePrimitiveType(ctx->typeTable, prtBool);
        break;
    case optRange:
        if (!isIntegerType(ctx->typeTable, type)) {
            logError(ctx->L,
                     &node->loc,
                     "cannot perform range binary operation '{s}' on "
                     "type '{t}'",
                     (FormatArg[]){{.s = getBinaryOpString(op)}, {.t = type}});
            return;
        }
        node->type = makeRangeType(ctx->typeTable);
        break;
    default:
        unreachable("");
    }
}

static void checkUnary(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    const Type *operand = evalType(visitor, node->unaryExpr.operand);
    // TODO check compatibility
    node->type = operand;
}

static void checkAssign(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    AstNode *left = node->assignExpr.lhs, *right = node->assignExpr.rhs;
    const Type *lhs = evalType(visitor, left);
    const Type *rhs = evalType(visitor, right);

    if (left->flags & flgConst) {
        logError(ctx->L,
                 &node->loc,
                 "lhs of assignment expressions is a constant",
                 (FormatArg[]){{.t = lhs}});
        node->type = sError;
    }
    // TODO check r-value-ness
    if (!isTypeAssignableFrom(ctx->typeTable, lhs, rhs)) {
        logError(ctx->L,
                 &node->assignExpr.rhs->loc,
                 "incompatible types on assigment expression, expecting '{t}', "
                 "got '{t}'",
                 (FormatArg[]){{.t = lhs}, {.t = rhs}});
        node->type = sError;
    }

    if (lhs == makeAutoType(ctx->typeTable)) {
        csAssert0(left->tag == astPath);
        const char *variable = left->path.elements->pathElement.name;
        AstNode *symbol = findSymbol(&ctx->env, ctx->L, variable, &left->loc);
        csAssert0(symbol);
        symbol->type = rhs;
        node->type = rhs;
    }
    else {
        node->type = lhs;
    }
}

static void checkAddressOf(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    const Type *operand = evalType(visitor, node->unaryExpr.operand);
    node->type = makePointerType(
        ctx->typeTable, operand, node->unaryExpr.operand->flags & flgConst);
}

static void checkIndex(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    const Type *target = evalType(visitor, node->indexExpr.target);
    u64 count = checkMany(visitor, node->indexExpr.indices);

    if (target->tag == typArray) {
        if (target->array.arity != count) {
            logError(ctx->L,
                     &getLastAstNode(node->indexExpr.indices)->loc,
                     "invalid number of indexes to type '{t}', expecting "
                     "'{u64}' but got '{u64}'",
                     (FormatArg[]){{.t = target},
                                   {.u64 = target->array.arity},
                                   {.u64 = count}});
        }
        node->type = target->array.elementType;
    }
    else if (target->tag == typMap) {
        if (count > 1) {
            logError(ctx->L,
                     &node->indexExpr.indices->next->loc,
                     "invalid indexes passed to map type '{t}', expecting "
                     "'1' but got '{u64}'",
                     (FormatArg[]){{.t = target}, {.u64 = count}});
        }
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
        node->type = makeTupleType(ctx->typeTable, args, count);
    }

    free(args);
}

static void checkGroupExpr(AstVisitor *visitor, AstNode *node)
{
    node->type = evalType(visitor, node->groupExpr.expr);
}

static void checkCall(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    const Type *callee = evalType(visitor, node->callExpr.callee);
    AstNode *arg = node->callExpr.args;

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
    if (callee->flags & flgClosure) {
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
        if (!isTypeAssignableFrom(
                ctx->typeTable, callee->func.params[i], type)) {
            logError(ctx->L,
                     &arg->loc,
                     "incompatible argument types, expecting '{t}' but got "
                     "'{t}'",
                     (FormatArg[]){{.t = callee->func.params[i]}, {.t = type}});
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
    const Type *ret =
        func && func->funcDecl.ret ? func->funcDecl.ret->type : NULL;

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

static void checkIfStmt(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
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
}

static void checkWhileStmt(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
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
    u64 count = countAstNodes(node->arrayType.dims);
    const u64 *indexes = mallocOrDie(sizeof(u64) * count);
    // TODO evaluate indexes
    node->type = makeArrayType(ctx->typeTable, element, indexes, count);

    free((void *)indexes);
}

static void checkTypeDecl(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    defineSymbol(&ctx->env, ctx->L, node->typeDecl.name, node);
    const Type *ref = evalType(visitor, node->typeDecl.aliased);
    node->type = makeAliasType(ctx->typeTable, ref, node->typeDecl.name);
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
        node->type = makeTupleType(ctx->typeTable, args, count);

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
        [astTupleExpr] = checkTupleExpr,
        [astGroupExpr] = checkGroupExpr,
        [astCallExpr] = checkCall,
        [astClosureExpr] = checkClosure,
        [astTypedExpr] = checkTypedExpr,
        [astTernaryExpr] = checkTernaryExpr,
        [astBlockStmt] = checkBlock,
        [astReturnStmt] = checkReturn,
        [astDeferStmt] = checkDeferStmt,
        [astIfStmt] = checkIfStmt,
        [astWhileStmt] = checkWhileStmt,
        [astPrimitiveType] = checkPrimitiveType,
        [astArrayType] = checkArrayType,
        [astPointerType] = checkPointerType,
        [astTypeDecl] = checkTypeDecl,
        [astUnionDecl] = checkUnionDecl,
        [astTupleType] = checkTupleType,
        [astFuncType] = checkFuncType,
        [astVoidType] = checkBuiltinType,
        [astStringType] = checkBuiltinType,
    },
    .fallback = checkFallback);
    // clang-format off

    sError = makeErrorType(context.typeTable);

    astVisit(&visitor, program);
}
