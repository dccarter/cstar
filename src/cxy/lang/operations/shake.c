//
// Created by Carter Mbotho on 2023-07-06.
//
#include "lang/operations.h"

#include "lang/ast.h"
#include "lang/flag.h"
#include "lang/strings.h"
#include "lang/visitor.h"

#include "core/alloc.h"

typedef struct {
    Log *L;
    MemPool *pool;
    StrPool *strPool;
} ShakeAstContext;

static AstNode *makeTupleMemberExpr(ShakeAstContext *ctx, AstNode *tuple, u64 i)
{
    if (tuple == NULL)
        return NULL;

    if (nodeIs(tuple, TupleExpr)) {
        AstNode *arg = tuple->tupleExpr.elements;
        tuple->tupleExpr.elements = arg->next;
        arg->next = NULL;
        return arg;
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
                                          .intLiteral.value = (i64)i})}});
}

static void transformVariadicFunction(ShakeAstContext *ctx,
                                      AstNode *node,
                                      AstNode *param)
{
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

    *node = (AstNode){.tag = astGenericDecl,
                      .loc = node->loc,
                      .flags = node->flags | flgVariadic,
                      .next = node->next,
                      .genericDecl = {.decl = copyAstNode(ctx->pool, node),
                                      .params = genericParam,
                                      .paramsCount = 1,
                                      .inferrable = 0}};
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
        if (op != opNew)                                                       \
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
            ctx, &node->loc, "str", count, 0);

    case opTruthy:
        return reportIfUnexpectedNumberOfParameters(
            ctx, &node->loc, "!!", count, 0);

    case opCallOverload:
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
    return makeAstNode(
        ctx->pool,
        &init->loc,
        &(AstNode){.tag = astVarDecl,
                   .varDecl = {.names = makeGenIdent(
                                   ctx->pool, ctx->strPool, &init->loc, NULL),
                               .init = init}});
}

void shakeVariableDecl(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
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
    AstNode *cond = node->ifStmt.cond, *ifNode = node;
    if (nodeIs(cond, VarDecl)) {
        AstNode *var = makeAstNode(
            ctx->pool,
            &cond->loc,
            &(AstNode){
                .tag = astVarDecl,
                .flags = cond->flags,
                .next = duplicateAstNode(ctx->pool, node),
                .varDecl = {
                    .names = makeGenIdent(ctx->pool,
                                          ctx->strPool,
                                          &cond->varDecl.names->loc,
                                          NULL),
                    .init = cond->varDecl.init,
                    .type = cloneAstNode(&(CloneAstConfig){.pool = ctx->pool},
                                         cond->varDecl.type)}});

        ifNode = var->next;
        ifNode->ifStmt.cond = makePathFromIdent(ctx->pool, var->varDecl.names);
        if (!nodeIs(ifNode->ifStmt.body, BlockStmt)) {
            ifNode->ifStmt.body = makeAstNode(
                ctx->pool,
                &ifNode->ifStmt.body->loc,
                &(AstNode){.tag = astBlockStmt,
                           .blockStmt = {.stmts = ifNode->ifStmt.body}});
        }

        cond->next = ifNode->ifStmt.body->blockStmt.stmts;
        cond->varDecl.init = makeAstNode(
            ctx->pool,
            &cond->loc,
            &(AstNode){.tag = astUnaryExpr,
                       .unaryExpr = {.op = opAddrOf,
                                     .isPrefix = true,
                                     .operand = makePathFromIdent(
                                         ctx->pool, var->varDecl.names)}});
        ifNode->ifStmt.body->blockStmt.stmts = cond;
        *node = *var;
        ifNode = node->next;
    }

    astVisit(visitor, ifNode->ifStmt.body);
    if (!nodeIs(ifNode->ifStmt.body, BlockStmt)) {
        ifNode->ifStmt.body = makeAstNode(
            ctx->pool,
            &ifNode->ifStmt.body->loc,
            &(AstNode){.tag = astBlockStmt,
                       .blockStmt = {.stmts = ifNode->ifStmt.body}});
    }

    if (ifNode->ifStmt.otherwise) {
        AstNode *otherwise = ifNode->ifStmt.otherwise;
        astVisit(visitor, otherwise);
        if (!nodeIs(otherwise, BlockStmt) && !nodeIs(otherwise, IfStmt)) {
            ifNode->ifStmt.otherwise =
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
    AstNode *cond = node->whileStmt.cond, *whileNode = node;

    if (nodeIs(cond, VarDecl)) {
        AstNode *var = makeAstNode(
            ctx->pool,
            &cond->loc,
            &(AstNode){
                .tag = astVarDecl,
                .flags = cond->flags,
                .next = duplicateAstNode(ctx->pool, node),
                .varDecl = {
                    .names = makeGenIdent(ctx->pool,
                                          ctx->strPool,
                                          &cond->varDecl.names->loc,
                                          NULL),
                    .init = NULL,
                    .type = cloneAstNode(&(CloneAstConfig){.pool = ctx->pool},
                                         cond->varDecl.type)}});

        whileNode = var->next;
        whileNode->whileStmt.cond = makeAstNode(
            ctx->pool,
            &cond->loc,
            &(AstNode){.tag = astGroupExpr,
                       .groupExpr.expr = makeAstNode(
                           ctx->pool,
                           &cond->loc,
                           &(AstNode){.tag = astAssignExpr,
                                      .assignExpr = {
                                          .op = opAssign,
                                          .lhs = makePathFromIdent(
                                              ctx->pool, var->varDecl.names),
                                          .rhs = cond->varDecl.init}})});

        if (!nodeIs(whileNode->whileStmt.body, BlockStmt)) {
            whileNode->whileStmt.body = makeAstNode(
                ctx->pool,
                &whileNode->whileStmt.body->loc,
                &(AstNode){.tag = astBlockStmt,
                           .blockStmt = {.stmts = whileNode->whileStmt.body}});
        }

        cond->next = whileNode->whileStmt.body->blockStmt.stmts;
        cond->varDecl.init = makeAstNode(
            ctx->pool,
            &cond->loc,
            &(AstNode){.tag = astUnaryExpr,
                       .unaryExpr = {.op = opAddrOf,
                                     .isPrefix = true,
                                     .operand = makePathFromIdent(
                                         ctx->pool, var->varDecl.names)}});
        whileNode->whileStmt.body->blockStmt.stmts = cond;
        *node = *var;
        whileNode = node->next;
    }

    if (!nodeIs(whileNode->whileStmt.body, BlockStmt)) {
        whileNode->whileStmt.body = makeAstNode(
            ctx->pool,
            &whileNode->whileStmt.body->loc,
            &(AstNode){.tag = astBlockStmt,
                       .blockStmt = {.stmts = whileNode->whileStmt.body}});
    }

    astVisit(visitor, whileNode->whileStmt.body);
}

void shakeFuncDecl(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    AstNode *params = node->funcDecl.signature->params, *param = params;
    u16 required = 0, total = 0;

    bool hasDefaultParams = false, isVariadic = false;

    for (; param; param = param->next) {
        total++;
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

    if (isVariadic)
        transformVariadicFunction(ctx, node, param);
}

static void shakeGenericDecl(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    AstNode *decl = node->genericDecl.decl, *gparams = node->genericDecl.params,
            *gparam = gparams;
    node->genericDecl.paramsCount = countAstNodes(gparams);
    node->genericDecl.name = getDeclarationName(node->genericDecl.decl);

    astVisit(visitor, gparams);

    if (!nodeIs(decl, FuncDecl)) {
        astVisit(visitor, decl);
        return;
    }

    u16 *inferrable = mallocOrDie(sizeof(u16) * node->genericDecl.paramsCount);
    int index = -1;
    for (u16 i = 0; gparam; gparam = gparam->next, i++) {
        AstNode *fparam = decl->funcDecl.signature->params;
        for (; fparam; fparam = fparam->next) {
            AstNode *type = fparam->funcParam.type;
            if (hasFlag(fparam, Variadic))
                break;
            inferrable[i] = inferGenericParamFromFuncParam(gparam, type, i);
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
    astVisit(visitor, decl);
}

static void shakeStructDecl(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    astVisit(visitor, node->structDecl.base);
    astVisitManyNodes(visitor, node->structDecl.implements);
    astVisitManyNodes(visitor, node->structDecl.members);

    node->structDecl.sortedMembers =
        makeSortedNodes(ctx->pool, node->structDecl.members, NULL);
}

AstNode *shakeAstNode(CompilerDriver *driver, AstNode *node)
{
    ShakeAstContext context = {
        .L = driver->L, .pool = &driver->pool, .strPool = &driver->strPool};

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context, {
        [astVarDecl] = shakeVariableDecl,
        [astIfStmt] = shakeIfStmt,
        [astWhileStmt] = shakeWhileStmt,
        [astFuncDecl] = shakeFuncDecl,
        [astGenericDecl] = shakeGenericDecl,
        [astStructDecl] = shakeStructDecl,
    }, .fallback = astVisitFallbackVisitAll);
    // clang-format on

    astVisit(&visitor, node);

    return node;
}
