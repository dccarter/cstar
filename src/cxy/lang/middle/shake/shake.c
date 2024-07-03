//
// Created by Carter Mbotho on 2023-07-06.
//
#include "shake.h"

#include "lang/middle/builtins.h"
#include "lang/operations.h"

#include "lang/frontend/ast.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/visitor.h"

#include "core/alloc.h"

static AstNode *makeTupleMemberExpr(ShakeAstContext *ctx, AstNode *tuple, u64 i)
{
    if (tuple == NULL)
        return NULL;

    if (nodeIs(tuple, TupleExpr)) {
        // TODO collect
        AstNode *arg = getNodeAtIndex(tuple->tupleExpr.elements, i);
        return duplicateAstNode(ctx->pool, arg);
    }

    AstNode *target = NULL;
    if (nodeIs(tuple, VarDecl)) {
        target = makePathFromIdent(ctx->pool, tuple->varDecl.names);
    }
    else {
        csAssert0(nodeIs(tuple, Path) || nodeIs(tuple, Identifier));
        target = copyAstNode(ctx->pool, tuple);
    }

    return makeAstNode(
        ctx->pool,
        &tuple->loc,
        &(AstNode){
            .tag = astMemberExpr,
            .flags = tuple->flags,
            .memberExpr = {.target = target,
                           .member = makeAstNode(
                               ctx->pool,
                               &tuple->loc,
                               &(AstNode){.tag = astIntegerLit,
                                          .intLiteral.uValue = (u64)i})}});
}

static void transformVariadicFunction(ShakeAstContext *ctx,
                                      AstNode *node,
                                      AstNode *param)
{

    AstNode *parent = node->parentScope;
    bool isTransient = findAttribute(param, S_transient);
    node->flags |= flgVariadic;
    AstNode *genericParam = makeAstNode(
        ctx->pool,
        &param->funcParam.type->loc,
        &(AstNode){.tag = astGenericParam,
                   .flags = flgVariadic,
                   .genericParam = {.name = S__Variadic,
                                    .constraints = param->funcParam.type,
                                    .inferIndex = node->funcDecl.paramsCount}});
    param->funcParam.type = makePath(ctx->pool,
                                     &param->funcParam.type->loc,
                                     S__Variadic,
                                     param->funcParam.type->flags,
                                     NULL);

    if (nodeIs(parent, GenericDecl)) {
        AstNode *params = getLastAstNode(parent->genericDecl.params);
        if (params)
            params->next = genericParam;
        else
            parent->genericDecl.params = genericParam;
        parent->flags |= flgVariadic | (isTransient ? flgTransient : flgNone);
        if (parent->genericDecl.inferrable == -1)
            parent->genericDecl.inferrable = 0;
        else
            parent->genericDecl.inferrable++;
    }
    else {
        *node = (AstNode){.tag = astGenericDecl,
                          .loc = node->loc,
                          .flags = node->flags | flgVariadic |
                                   (isTransient ? flgTransient : flgNone),
                          .next = node->next,
                          .genericDecl = {.decl = copyAstNode(ctx->pool, node),
                                          .params = genericParam,
                                          .paramsCount = 1,
                                          .inferrable = 0}};
    }
}

attr(always_inline) static bool reportIfUnexpectedNumberOfParameters(
    ShakeAstContext *ctx,
    const FileLoc *loc,
    cstring name,
    u64 count,
    u64 expected)
{
    if (expected == count)
        return true;

    logError(ctx->L,
             loc,
             "unexpected numbers of parameters for {s} operator "
             "overload, expecting {u64}, got {u64}",
             (FormatArg[]){{.s = name}, {.u64 = expected}, {.u64 = count}});
    return false;
}

static bool validateOperatorOverloadArguments(ShakeAstContext *ctx,
                                              AstNode *node,
                                              u64 count)
{
    Operator op = node->funcDecl.operatorOverload;
    switch (op) {
#define f(OP, _0, _1, STR, ...)                                                \
    case op##OP:                                                               \
        return reportIfUnexpectedNumberOfParameters(                           \
            ctx, &node->loc, STR, count, op == opRange ? 0 : 1);
        AST_BINARY_EXPR_LIST(f)
#undef f

#define f(OP, _0, _1, STR, ...)                                                \
    case op##OP:                                                               \
        if (op != opInitOverload)                                              \
            return reportIfUnexpectedNumberOfParameters(                       \
                ctx, &node->loc, STR, count, 0);                               \
        else                                                                   \
            return true;

        AST_UNARY_EXPR_LIST(f)
#undef f

    case opIndexOverload:
        return reportIfUnexpectedNumberOfParameters(
            ctx, &node->loc, "[]", count, 1);

    case opIndexAssignOverload:
        return reportIfUnexpectedNumberOfParameters(
            ctx, &node->loc, "=[]", count, 2);

    case opStringOverload:
        return reportIfUnexpectedNumberOfParameters(
            ctx, &node->loc, "str", count, 1);

    case opHashOverload:
        return reportIfUnexpectedNumberOfParameters(
            ctx, &node->loc, "hash", count, 0);

    case opTruthy:
        return reportIfUnexpectedNumberOfParameters(
            ctx, &node->loc, "!!", count, 0);

    case opDeinitOverload:
        return reportIfUnexpectedNumberOfParameters(
            ctx, &node->loc, "deinit", count, 0);

    case opCallOverload:
    case opInitOverload:
        return true;

    default:
        return false;
    }
}

attr(always_inline) static u16
    inferGenericParamFromFuncParam(const AstNode *gparam,
                                   const AstNode *type,
                                   u16 index)
{
    if (!nodeIs(type, Path))
        return 0;
    AstNode *elem = type->path.elements;
    if (elem->next || elem->pathElement.args)
        return 0;

    return gparam->genericParam.name == elem->pathElement.name ? index + 1 : 0;
}

static AstNode *shakeVariableInitializer(ShakeAstContext *ctx, AstNode *init)
{
    if (init == NULL || nodeIs(init, TupleExpr) || nodeIs(init, Identifier) ||
        (nodeIs(init, Path) && init->path.elements->next == NULL))
        return init;

    // Create variable for this
    return makeVarDecl(ctx->pool,
                       &init->loc,
                       init->flags,
                       makeAnonymousVariable(ctx->strings, "_gi"),
                       NULL,
                       init,
                       NULL,
                       NULL);
}

static AstNode *makeStrExprBuilder(ShakeAstContext *ctx, AstNode *node)
{
    AstNode *sb = findBuiltinDecl(S_String);
    csAssert0(sb);

    return makeVarDecl(
        ctx->pool,
        &node->loc,
        flgNone,
        makeAnonymousVariable(ctx->strings, S_sb),
        NULL,
        makeCallExpr(
            ctx->pool,
            &node->loc,
            makeResolvedPath(
                ctx->pool, &node->loc, S_String, flgNone, sb, NULL, sb->type),
            NULL,
            flgNone,
            NULL,
            NULL),
        NULL,
        NULL);
}

void shakeVariableDecl(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);

    astVisit(visitor, node->varDecl.type);
    astVisit(visitor, node->varDecl.init);

    AstNode *names = node->varDecl.names, *init = node->varDecl.init,
            *type = node->varDecl.type, *name = names;

    if (names->next == NULL) {
        node->varDecl.name = names->ident.value;
        return;
    }

    AstNode *tuple = shakeVariableInitializer(ctx, init);

    AstNode *vars = NULL, *it = NULL;
    u64 i = 0;
    for (; name; i++) {
        AstNode *name_ = name;
        name = name->next;
        name_->next = NULL;

        if (isIgnoreVar(name_->ident.value)) {
            if (tuple == NULL) {
                logError(ctx->L,
                         &name_->loc,
                         "cannot use the builtin ignore `_` variable when "
                         "multi-variable declaration has no expression",
                         NULL);
                return;
            }
            continue;
        }

        AstNode *var = makeAstNode(
            ctx->pool,
            &name_->loc,
            &(AstNode){
                .tag = astVarDecl,
                .flags = node->flags | flgVisited,
                .varDecl = {.name = name_->ident.value,
                            .names = name_,
                            .type = copyAstNode(ctx->pool, type),
                            .init = makeTupleMemberExpr(ctx, tuple, i)}});
        if (vars == NULL) {
            vars = var;
            it = var;
        }
        else {
            it->next = var;
            it = var;
        }
    }

    it->next = node->next;
    if (tuple == init) {
        *node = *vars;
    }
    else {
        tuple->next = vars;
        *node = *tuple;
    }
}

void shakeIfStmt(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    AstNode *cond = node->ifStmt.cond;
    if (nodeIs(cond, VarDecl)) {
        AstNode *var = duplicateAstNode(ctx->pool, cond);
        var->varDecl.name = var->varDecl.names->ident.value;

        astModifierAdd(&ctx->block, var);

        cond->tag = astPath;
        cond->path.elements = makePathElement(
            ctx->pool, &cond->loc, var->varDecl.name, var->flags, NULL, NULL);

        if (!nodeIs(node->ifStmt.body, BlockStmt)) {
            node->ifStmt.body = makeAstNode(
                ctx->pool,
                &node->ifStmt.body->loc,
                &(AstNode){.tag = astBlockStmt,
                           .blockStmt = {.stmts = node->ifStmt.body}});
        }
    }

    astVisit(visitor, node->ifStmt.body);
    if (!nodeIs(node->ifStmt.body, BlockStmt)) {
        node->ifStmt.body =
            makeAstNode(ctx->pool,
                        &node->ifStmt.body->loc,
                        &(AstNode){.tag = astBlockStmt,
                                   .blockStmt = {.stmts = node->ifStmt.body}});
    }

    if (node->ifStmt.otherwise) {
        AstNode *otherwise = node->ifStmt.otherwise;
        astVisit(visitor, otherwise);
        if (!nodeIs(otherwise, BlockStmt) && !nodeIs(otherwise, IfStmt)) {
            node->ifStmt.otherwise =
                makeAstNode(ctx->pool,
                            &otherwise->loc,
                            &(AstNode){.tag = astBlockStmt,
                                       .blockStmt = {.stmts = otherwise}});
        }
    }
}

void shakeWhileStmt(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    AstNode *cond = node->whileStmt.cond;

    if (nodeIs(cond, VarDecl)) {
        AstNode *var = duplicateAstNode(ctx->pool, cond);
        var->varDecl.name = var->varDecl.names->ident.value;

        astModifierAdd(&ctx->block, var);

        cond->tag = astGroupExpr;
        cond->groupExpr.expr = makeAstNode(
            ctx->pool,
            &cond->loc,
            &(AstNode){.tag = astAssignExpr,
                       .assignExpr = {.op = opAssign,
                                      .lhs = makePathFromIdent(
                                          ctx->pool, var->varDecl.names),
                                      .rhs = var->varDecl.init}});
        var->varDecl.init = NULL;

        if (!nodeIs(node->whileStmt.body, BlockStmt)) {
            node->whileStmt.body = makeAstNode(
                ctx->pool,
                &node->whileStmt.body->loc,
                &(AstNode){.tag = astBlockStmt,
                           .blockStmt = {.stmts = node->whileStmt.body}});
        }
    }

    if (!nodeIs(node->whileStmt.body, BlockStmt)) {
        node->whileStmt.body = makeAstNode(
            ctx->pool,
            &node->whileStmt.body->loc,
            &(AstNode){.tag = astBlockStmt,
                       .blockStmt = {.stmts = node->whileStmt.body}});
    }

    astVisit(visitor, node->whileStmt.body);
}

void shakeFuncDecl(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    u16 required = 0, total = 0;
    node->flags |= findAttribute(node, S_pure) == NULL ? flgNone : flgPure;
    bool hasDefaultParams = false, isVariadic = false;

    AstNode *params = node->funcDecl.signature->params, *param = params;
    for (; param; param = param->next) {
        total++;
        astVisit(visitor, param);

        if (param->funcParam.def) {
            if (!hasDefaultParams) {
                required = total;
            }
            hasDefaultParams = true;
        }
        else if (hasDefaultParams && !hasFlag(param, Variadic)) {
            logError(ctx->L,
                     &param->loc,
                     "declaring a parameter without a default value not "
                     "allowed after one declared with a default value",
                     NULL);
            return;
        }
        else if (hasFlag(param, Variadic)) {
            // transform to variadic function
            if (param->next) {
                logError(ctx->L,
                         &param->next->loc,
                         "variadic parameter should be the last parameter",
                         NULL);
                return;
            }
            isVariadic = true;
            break;
        }
        else {
            required++;
        }
    }

    if (node->funcDecl.operatorOverload != opInvalid) {
        if (!validateOperatorOverloadArguments(ctx, node, total))
            return;
    }

    node->funcDecl.paramsCount = total;
    node->funcDecl.requiredParamsCount = required;
    astVisit(visitor, node->funcDecl.body);

    if (node->funcDecl.body && !nodeIs(node->funcDecl.body, BlockStmt)) {
        node->funcDecl.body = makeAstNode(
            ctx->pool,
            &node->funcDecl.body->loc,
            &(AstNode){
                .tag = astBlockStmt,
                .blockStmt = {
                    .stmts = makeAstNode(
                        ctx->pool,
                        &node->funcDecl.body->loc,
                        &(AstNode){.tag = astReturnStmt,
                                   .returnStmt.expr = node->funcDecl.body})}});
    }

    if (isVariadic) {
        if (!hasFlag(node, Extern)) {
            transformVariadicFunction(ctx, node, param);
        }
        else {
            node->flags |= flgVariadic;
        }
    }
}

void shakeCallExpr(AstVisitor *visitor, AstNode *node)
{
    if (findAttribute(node, S_sync))
        node->flags |= flgSyncCall;
    astVisitFallbackVisitAll(visitor, node);
}

void shakeGroupExpr(AstVisitor *visitor, AstNode *node)
{
    AstNode *expr = node->groupExpr.expr;
    if (nodeIs(expr, BlockStmt)) {
        expr->flags |= flgBlockReturns;
        node->tag = astStmtExpr;
        node->stmtExpr.stmt = expr;
    }

    astVisitFallbackVisitAll(visitor, node);
}

void shakeExprStmt(AstVisitor *visitor, AstNode *node)
{
    if (nodeIs(node->exprStmt.expr, CallExpr) && findAttribute(node, S_sync)) {
        node->exprStmt.expr->flags |= flgSyncCall;
    }
    astVisitFallbackVisitAll(visitor, node);
}

void shakeMatchStmt(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->matchStmt.expr;
    astVisit(visitor, expr);
    if (!nodeIsLeftValue(expr)) {
        AstNode *var =
            makeVarDecl(ctx->pool,
                        &expr->loc,
                        flgNone,
                        makeAnonymousVariable(ctx->strings, "_match"),
                        NULL,
                        expr,
                        NULL,
                        NULL);
        node->matchStmt.expr =
            makePath(ctx->pool, &expr->loc, var->varDecl.name, flgNone, NULL);

        astModifierAdd(&ctx->block, var);
    }
    astVisitManyNodes(visitor, node->matchStmt.cases);
}

static void shakeGenericDecl(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    AstNode *decl = node->genericDecl.decl, *gparams = node->genericDecl.params,
            *gparam = gparams;
    node->genericDecl.paramsCount = countAstNodes(gparams);
    node->genericDecl.name = getDeclarationName(node->genericDecl.decl);
    if (findAttribute(node, S_pure))
        node->genericDecl.decl->flags |= flgPure;

    astVisit(visitor, gparams);

    if (!nodeIs(decl, FuncDecl)) {
        astVisit(visitor, decl);
        return;
    }

    u16 *inferrable = mallocOrDie(sizeof(u16) * node->genericDecl.paramsCount);
    int index = -1;
    for (u16 i = 0; gparam; gparam = gparam->next, i++) {
        if (gparam->genericParam.defaultValue != NULL) {
            logWarning(ctx->L,
                       &gparam->loc,
                       "default values on function generic decl params not "
                       "supported, ignoring",
                       NULL);
        }
        AstNode *fparam = decl->funcDecl.signature->params;
        for (u16 j = 0; fparam; fparam = fparam->next, j++) {
            AstNode *type = fparam->funcParam.type;
            if (hasFlag(fparam, Variadic))
                break;
            inferrable[i] = inferGenericParamFromFuncParam(gparam, type, j);
            if (inferrable[i]) {
                index = index == -1 ? i : index;
                break;
            }
        }
        if (!inferrable[i])
            index = -1;
    }

    if (index >= 0) {
        gparam = gparams;
        for (u64 i = 0; gparam; gparam = gparam->next, i++) {
            if (i >= index)
                gparam->genericParam.inferIndex = inferrable[i];
        }
    }
    else if (decl->funcDecl.operatorOverload != opInvalid) {
        logError(ctx->L,
                 &node->loc,
                 "unsupported generic function overload, must be inferrable",
                 NULL);
        return;
    }

    free(inferrable);
    node->genericDecl.inferrable = (i16)index;
    decl->parentScope = node;
    astVisit(visitor, decl);
}

static void shakeForStmt(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    AstNode *variable = node->forStmt.var, *body = node->forStmt.body;
    u64 flags = variable->flags;
    AstNode *name = variable->varDecl.names;
    if (name->next) {
        AstNode *it = name->next;
        variable->varDecl.name = name->ident.value;
        variable->loc = name->loc;
        name->next = NULL;
        for (; it;) {
            variable->next = makeVarDecl(ctx->pool,
                                         &it->loc,
                                         flags,
                                         it->ident.value,
                                         NULL,
                                         NULL,
                                         NULL,
                                         NULL);
            name = it;
            it = it->next;
            name->next = NULL;
            variable = variable->next;
        }
    }
    else {
        variable->varDecl.name = name->ident.value;
    }
    astVisit(visitor, node->forStmt.range);

    if (!hasFlag(node, Comptime) && !nodeIs(body, BlockStmt))
        node->forStmt.body =
            makeBlockStmt(ctx->pool, &body->loc, body, NULL, NULL);
    astVisit(visitor, node->forStmt.body);
}

static void shakeBlockStmt(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    astModifierInit(&ctx->block, node);

    AstNode *stmt = node->blockStmt.stmts;
    for (; stmt; stmt = stmt->next) {
        astModifierNext(&ctx->block, stmt);
        astVisit(visitor, stmt);
    }
}

static void shakeArrayType(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    if (node->arrayType.dim == NULL) {
        AstNode *slice = findBuiltinDecl(S_Slice);
        csAssert0(slice);
        node->tag = astPath;
        node->type = NULL;
        node->path.elements =
            makeResolvedPathElementWithArgs(ctx->pool,
                                            &node->loc,
                                            S_Slice,
                                            flgNone,
                                            slice,
                                            NULL,
                                            node->arrayType.elementType,
                                            NULL);
    }
}

static void shakeStringExpr(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    AstNode *part = node->stringExpr.parts;
    if (nodeIs(part, StringLit) && part->next == NULL) {
        node->tag = astStringLit;
        node->stringLiteral = part->stringLiteral;
        return;
    }

    AstNode *var = makeStrExprBuilder(ctx, node),
            *sb = makeResolvedPath(ctx->pool,
                                   &node->loc,
                                   var->varDecl.name,
                                   flgNone,
                                   var,
                                   NULL,
                                   var->type);

    for (; part;) {
        if (nodeIs(part, StringLit) && part->stringLiteral.value[0] == '\0') {
            part = part->next;
            continue;
        }

        sb = makeBinaryExpr(
            ctx->pool, &node->loc, flgNone, sb, opShl, part, NULL, NULL);
        part = part->next;
        sb->binaryExpr.rhs->next = NULL;
    }

    var->next = sb;
    astModifierAdd(&ctx->block, var);

    node->tag = astPath;
    node->path.elements = makePathElement(
        ctx->pool, &node->loc, var->varDecl.name, var->flags, NULL, NULL);
}

static void withSavedStack(Visitor func, AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    __typeof(ctx->stack) stack = ctx->stack;

    func(visitor, node);

    ctx->stack = stack;
}

AstNode *shakeAstNode(CompilerDriver *driver, AstNode *node)
{
    ShakeAstContext context = {
        .L = driver->L, .pool = driver->pool, .strings = driver->strings};

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context, {
        [astVarDecl] = shakeVariableDecl,
        [astIfStmt] = shakeIfStmt,
        [astWhileStmt] = shakeWhileStmt,
        [astForStmt] = shakeForStmt,
        [astFuncDecl] = shakeFuncDecl,
        [astGenericDecl] = shakeGenericDecl,
        [astBlockStmt] = shakeBlockStmt,
        [astStringExpr] = shakeStringExpr,
        [astClosureExpr] = shakeClosureExpr,
        [astCallExpr] = shakeCallExpr,
        [astGroupExpr] = shakeGroupExpr,
        [astExprStmt] = shakeExprStmt,
        [astMatchStmt] = shakeMatchStmt,
        [astArrayType] = shakeArrayType
    }, .fallback = astVisitFallbackVisitAll, .dispatch = withSavedStack);
    // clang-format on

    astVisit(&visitor, node);

    return node;
}
