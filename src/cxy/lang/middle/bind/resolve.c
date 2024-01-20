//
// Created by Carter Mbotho on 2023-07-14.
//

#include "bind.h"

#include "lang/middle/builtins.h"
#include "lang/middle/macro.h"
#include "lang/middle/scope.h"
#include "lang/operations.h"

#include "lang/frontend/capture.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/visitor.h"

static inline bool isCallableDecl(AstNode *node)
{
    return nodeIs(node, FuncDecl) || nodeIs(node, MacroDecl) ||
           (nodeIs(node, GenericDecl) &&
            nodeIs(node->genericDecl.decl, FuncDecl));
}

static inline bool shouldCaptureSymbol(const AstNode *closure,
                                       const AstNode *symbol)
{
    return closure && (nodeIs(symbol, VarDecl) || nodeIs(symbol, FuncParam) ||
                       nodeIs(symbol, Field));
}

static void captureSymbol(AstNode *closure, AstNode *node, AstNode *symbol)
{
    if (!shouldCaptureSymbol(closure, symbol))
        return;

    AstNode *root = node->path.elements;
    AstNode *parent = symbol->parentScope;

    if (nodeIs(symbol, FuncParam) && parent == closure)
        return;

    parent = node->parentScope;
    Capture *prev = NULL;
    while (parent && parent != symbol->parentScope) {
        if (nodeIs(parent, ClosureExpr)) {
            if (nodeIs(symbol, FuncParam) && symbol->parentScope == parent)
                return;

            // capture in current set
            if (prev)
                prev->flags = flgMember;

            prev = addClosureCapture(&parent->closureExpr.captureSet, symbol);
            root->flags |= flgMember;
            if (nodeIs(symbol, Field))
                prev->flags |= flgMember;
        }
        parent = parent->parentScope;
    }
}

static AstNode *resolvePathBaseUpChain(BindContext *ctx, AstNode *path)
{
    AstNode *root = path->path.elements;
    AstNode *parent = findEnclosingClassOrStruct(ctx->env, NULL, NULL, NULL);
    if (!nodeIs(parent, ClassDecl) || parent->classDecl.base == NULL ||
        !nodeIs(parent->classDecl.base, Path)) //
    {
        return findSymbol(ctx->env,
                          ctx->L,
                          root->pathElement.alt ?: root->pathElement.name,
                          &root->loc);
    }

    AstNode *resolved = findSymbol(
        ctx->env, NULL, root->pathElement.alt ?: root->pathElement.name, NULL);

    if (resolved)
        return resolved;

    AstNode *base = resolvePath(parent->classDecl.base);

    // lookup symbol upstream
    for (u64 i = 1; isStructDeclaration(base);
         base = resolvePath(underlyingDeclaration(base)->classDecl.base), i++) {
        resolved = findInAstNode(
            base, root->pathElement.alt ?: root->pathElement.name);
        if (resolved) {
            path->path.inheritanceDepth = i;

            path->path.elements = makeAstNode(
                ctx->pool,
                &root->loc,
                &(AstNode){.tag = astPathElem,
                           .next = root,
                           .pathElement = {.name = base->structDecl.name,
                                           .resolvesTo =
                                               nodeIs(base, GenericDecl)
                                                   ? underlyingDeclaration(base)
                                                   : NULL}});
            if (nodeIs(base, GenericDecl))
                path->flags |= flgInherited;

            return resolved;
        }
    }

    logError(
        ctx->L,
        &root->loc,
        "undefined symbol '{s}'",
        (FormatArg[]){{.s = root->pathElement.alt ?: root->pathElement.name}});
    suggestSimilarSymbol(ctx->env, ctx->L, root->pathElement.name);

    return NULL;
}

void bindPath(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    AstNode *base = node->path.elements;
    if (!base->pathElement.isKeyword) {
        AstNode *resolved = resolvePathBaseUpChain(ctx, node);
        if (resolved == NULL)
            return;
        if (hasFlag(resolved, Comptime) && !ctx->isComptimeContext) {
            logError(ctx->L,
                     &base->loc,
                     "comptime variable cannot be assigned outside comptime "
                     "context, did you mean `#{{{s}}`",
                     (FormatArg[]){{.s = base->pathElement.name}});
            logNote(ctx->L,
                    &resolved->loc,
                    "comptime variable declared here",
                    NULL);
            return;
        }

        // capture symbol if in closure
        base->pathElement.resolvesTo = resolved;
        captureSymbol(ctx->currentClosure, node, base->pathElement.resolvesTo);
    }
    else {
        cstring keyword = base->pathElement.name;
        if (keyword == S_This) {
            base->pathElement.enclosure =
                findEnclosingClassOrStruct(ctx->env, NULL, keyword, NULL);
            if (base->pathElement.enclosure == NULL) {
                logError(
                    ctx->L,
                    &base->loc,
                    "'This' keyword must be used inside a class or struct decl",
                    NULL);
                return;
            }
        }
        else {
            AstNode *func =
                findEnclosingFunction(ctx->env, NULL, keyword, NULL);
            if (func == NULL || func->funcDecl.this_ == NULL) {
                logError(ctx->L,
                         &base->loc,
                         "'{s}' keyword must be used inside a member function",
                         (FormatArg[]){{.s = keyword}});
                return;
            }

            base->pathElement.resolvesTo = func->funcDecl.this_;
            base->flags |= (func->flags & flgConst);

            if (keyword == S_super) {
                AstNode *parent =
                    findEnclosingClassOrStruct(ctx->env, NULL, keyword, NULL);
                if (parent->classDecl.base == NULL) {
                    logError(ctx->L,
                             &base->loc,
                             "keyword 'super' can only be used within a class "
                             "which extends a base class",
                             NULL);
                    return;
                }
                base->flags |= flgMember;
            }
        }
    }

    base->flags |= base->pathElement.resolvesTo->flags;
    for (AstNode *elem = base; elem; elem = elem->next)
        astVisitManyNodes(visitor, elem->pathElement.args);
    node->flags |= base->pathElement.resolvesTo->flags;
}

void bindIdentifier(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    node->ident.resolvesTo = findSymbol(
        ctx->env, ctx->L, node->ident.alias ?: node->ident.value, &node->loc);
}

void bindGenericParam(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    astVisitManyNodes(visitor, node->genericParam.constraints);
    defineSymbol(ctx->env, ctx->L, node->genericParam.name, node);
}

void bindGenericDecl(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    pushScope(ctx->env, node);
    astVisitManyNodes(visitor, node->genericDecl.params);
    astVisit(visitor, node->genericDecl.decl);
    popScope(ctx->env);
}

void bindFuncParam(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    node->flags |= findAttribute(node, S_transient) ? flgTransient : flgNone;
    astVisit(visitor, node->funcParam.type);
    astVisit(visitor, node->funcParam.def);
    node->flags |= (node->funcParam.type->flags & flgConst);
    defineSymbol(ctx->env, ctx->L, node->funcParam.name, node);
}

void bindFunctionDecl(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    pushScope(ctx->env, node);

    if (findEnclosingClassOrStruct(ctx->env, NULL, S_this, NULL) &&
        findAttribute(node, S_static) == NULL) {
        node->funcDecl.this_ =
            makeFunctionParam(ctx->pool,
                              &node->loc,
                              S_this,
                              NULL,
                              NULL,
                              node->flags & flgConst,
                              node->funcDecl.signature->params);
        defineSymbol(ctx->env, ctx->L, node->funcParam.name, node);
    }

    astVisit(visitor, node->funcDecl.signature->ret);
    astVisitManyNodes(visitor, node->funcDecl.signature->params);
    astVisit(visitor, node->funcDecl.body);

    popScope(ctx->env);
}

void bindFuncType(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    pushScope(ctx->env, node);
    astVisit(visitor, node->funcType.ret);
    astVisitManyNodes(visitor, node->funcType.params);
    popScope(ctx->env);
}

void bindMacroDecl(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);

    pushScope(ctx->env, node);

    astVisit(visitor, node->macroDecl.ret);
    astVisitManyNodes(visitor, node->macroDecl.params);
    astVisit(visitor, node->macroDecl.body);

    popScope(ctx->env);
}

void bindVarDecl(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    AstNode *name = node->varDecl.names;
    if (hasFlag(name, Comptime))
        astVisit(visitor, name);
    astVisit(visitor, node->varDecl.type);
    astVisit(visitor, node->varDecl.init);
    if (node->varDecl.init)
        node->varDecl.init->parentScope = node;
    for (; name; name = name->next)
        defineDeclaration(ctx, name->ident.value, node);
}

void bindTypeDecl(AstVisitor *visitor, AstNode *node)
{
    astVisit(visitor, node->typeDecl.aliased);
}

void bindUnionDecl(AstVisitor *visitor, AstNode *node)
{
    astVisitManyNodes(visitor, node->unionDecl.members);
}

void bindEnumOption(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    astVisit(visitor, node->enumOption.value);
    defineSymbol(ctx->env, ctx->L, node->enumOption.name, node);
}

void bindEnumDecl(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);

    astVisit(visitor, node->enumDecl.base);

    pushScope(ctx->env, node);
    AstNode *option = node->enumDecl.options;
    i64 nextValue = 0, i = 0;
    for (; option; option = option->next, i++) {
        option->enumOption.index = i;
        option->flags |= flgMember | flgEnumLiteral;
        astVisit(visitor, option);

        if (option->enumOption.value == NULL) {
            option->enumOption.value = makeIntegerLiteral(
                ctx->pool, &option->loc, nextValue++, NULL, NULL);
            continue;
        }

        AstNode *value = option->enumOption.value;
        if (nodeIs(value, IntegerLit)) {
            nextValue = integerLiteralValue(value) + 1;
            continue;
        }

        TODO("support paths when eval is implemented");
    }
    node->enumDecl.len = i;
    popScope(ctx->env);
}

void bindStructField(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    astVisit(visitor, node->structField.type);
    astVisit(visitor, node->structField.value);
    defineSymbol(ctx->env, ctx->L, node->structField.name, node);
}

void bindStructOrClassDecl(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    AstNode *member = node->structDecl.members;

    astVisit(visitor, node->structDecl.base);
    astVisitManyNodes(visitor, node->structDecl.implements);

    pushScope(ctx->env, node);
    defineSymbol(ctx->env, ctx->L, S_This, node);

    for (; member; member = member->next) {
        if (isCallableDecl(member)) {
            if (nodeIs(member, FuncDecl)) {
                defineFunctionDecl(
                    ctx->env, ctx->L, getDeclarationName(member), member);
            }
            else {
                defineSymbol(
                    ctx->env, ctx->L, getDeclarationName(member), member);
            }
        }
        else {
            astVisit(visitor, member);
        }
    }

    if (nodeIs(node, ClassDecl) || (!isBuiltinsInitialized()))
        defineDeclaration(ctx, node->structDecl.name, node);
    member = node->structDecl.members;
    for (; member; member = member->next) {
        // visit unvisited functions or macros
        if (isCallableDecl(member))
            astVisit(visitor, member);
    }

    popScope(ctx->env);
}

void bindInterfaceDecl(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    AstNode *member = node->interfaceDecl.members;

    pushScope(ctx->env, node);
    for (; member; member = member->next) {
        astVisit(visitor, member);
    }
    popScope(ctx->env);
}

void bindIfStmt(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);

    pushScope(ctx->env, node);
    astVisit(visitor, node->ifStmt.cond);
    astVisit(visitor, node->ifStmt.body);
    astVisit(visitor, node->ifStmt.otherwise);
    popScope(ctx->env);
}

void bindClosureExpr(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);

    astVisit(visitor, node->closureExpr.ret);

    pushScope(ctx->env, node);
    astVisitManyNodes(visitor, node->closureExpr.params);
    ctx->currentClosure = node;
    astVisit(visitor, node->closureExpr.body);
    ctx->currentClosure = NULL;
    popScope(ctx->env);

    Capture *capture = allocFromMemPool(
        ctx->pool, sizeof(Capture) * node->closureExpr.captureSet.index);

    node->closureExpr.captureCount =
        getOrderedCapture(&node->closureExpr.captureSet,
                          capture,
                          node->closureExpr.captureSet.index);

    node->closureExpr.capture = capture;
}

void bindDeferStmt(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);

    astVisit(visitor, node->deferStmt.expr);
    node->deferStmt.block =
        findEnclosingBlock(ctx->env, "defer", ctx->L, &node->loc);
}

void bindBreakOrContinueStmt(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    node->continueExpr.loop =
        findEnclosingLoop(ctx->env,
                          ctx->L,
                          nodeIs(node, ContinueStmt) ? "continue" : "break",
                          &node->loc);
}

void bindReturnStmt(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);

    astVisit(visitor, node->returnStmt.expr);
    node->returnStmt.func =
        findEnclosingFunctionOrClosure(ctx->env, ctx->L, &node->loc);
}

void bindBlockStmt(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    pushScope(ctx->env, node);
    astVisitManyNodes(visitor, node->blockStmt.stmts);
    popScope(ctx->env);
}

void bindForStmt(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    pushScope(ctx->env, node);

    astVisit(visitor, node->forStmt.range);
    astVisitManyNodes(visitor, node->forStmt.var);
    astVisit(visitor, node->forStmt.body);

    popScope(ctx->env);
}

void bindWhileStmt(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    pushScope(ctx->env, node);

    astVisit(visitor, node->whileStmt.cond);
    astVisit(visitor, node->whileStmt.body);

    popScope(ctx->env);
}

void bindSwitchStmt(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    pushScope(ctx->env, node);

    astVisit(visitor, node->switchStmt.cond);
    astVisitManyNodes(visitor, node->switchStmt.cases);

    popScope(ctx->env);
}

void bindMatchStmt(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    pushScope(ctx->env, node);

    astVisit(visitor, node->matchStmt.expr);
    astVisitManyNodes(visitor, node->matchStmt.cases);

    popScope(ctx->env);
}

void bindCaseStmt(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    pushScope(ctx->env, node);

    astVisit(visitor, node->caseStmt.match);
    if (node->caseStmt.variable) {
        defineSymbol(ctx->env,
                     ctx->L,
                     node->caseStmt.variable->ident.value,
                     node->caseStmt.variable);
    }
    astVisit(visitor, node->caseStmt.body);

    popScope(ctx->env);
}

void bindMacroCallExpr(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    EvaluateMacro macro = findBuiltinMacroByNode(node->macroCallExpr.callee);
    if (macro == NULL) {
        logError(ctx->L,
                 &node->macroCallExpr.callee->loc,
                 "currently only native macros are supported",
                 NULL);
        return;
    }

    node->macroCallExpr.evaluator = macro;
    astVisitManyNodes(visitor, node->macroCallExpr.args);
}

void bindMemberExpr(AstVisitor *visitor, AstNode *node)
{
    AstNode *member = node->memberExpr.member;
    astVisit(visitor, node->memberExpr.target);
    if (hasFlag(member, Comptime))
        astVisit(visitor, member);
}

void bindProgram(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    ctx->root.parent = node;
    AstNode *decl = node->program.decls;
    for (; decl; decl = decl->next) {
        ctx->root.previous = ctx->root.current;
        ctx->root.current = decl;
        astVisit(visitor, decl);
    }
}

void withParentScope(Visitor func, AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    if (node && ctx->env->scope && !nodeIs(node, Program)) {
        node->parentScope = ctx->env->scope->node;
    }

    bool isComptime = hasFlag(node, Comptime);
    __typeof(ctx->stack) stack = ctx->stack;

    if (!ctx->isComptimeContext && isComptime)
        ctx->isComptimeContext = isComptime;
    func(visitor, node);
    ctx->stack = stack;
}

void bindAstPhase2(CompilerDriver *driver, Env *env, AstNode *node)
{
    BindContext context = {.env = env, .L = driver->L, .pool = &driver->pool};

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context, {
        [astProgram] = bindProgram,
        [astIdentifier] = bindIdentifier,
        [astGenericParam] = bindGenericParam,
        [astGenericDecl] = bindGenericDecl,
        [astPath] = bindPath,
        [astFuncType] = bindFuncType,
        [astFuncDecl] = bindFunctionDecl,
        [astMacroDecl] = bindMacroDecl,
        [astFuncParam] = bindFuncParam,
        [astVarDecl] = bindVarDecl,
        [astTypeDecl] = bindTypeDecl,
        [astUnionDecl] = bindUnionDecl,
        [astEnumOption] = bindEnumOption,
        [astEnumDecl] = bindEnumDecl,
        [astField] = bindStructField,
        [astStructDecl] = bindStructOrClassDecl,
        [astClassDecl] = bindStructOrClassDecl,
        [astInterfaceDecl] = bindInterfaceDecl,
        [astIfStmt] = bindIfStmt,
        [astClosureExpr] = bindClosureExpr,
        [astDeferStmt] = bindDeferStmt,
        [astBreakStmt] = bindBreakOrContinueStmt,
        [astContinueStmt] = bindBreakOrContinueStmt,
        [astReturnStmt] = bindReturnStmt,
        [astBlockStmt] = bindBlockStmt,
        [astForStmt] = bindForStmt,
        [astWhileStmt] = bindWhileStmt,
        [astSwitchStmt] = bindSwitchStmt,
        [astMatchStmt] = bindMatchStmt,
        [astCaseStmt] = bindCaseStmt,
        [astMacroCallExpr] = bindMacroCallExpr,
        [astMemberExpr] = bindMemberExpr
    }, .fallback = astVisitFallbackVisitAll, .dispatch = withParentScope);
    // clang-format on

    astVisit(&visitor, node);
}
