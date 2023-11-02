//
// Created by Carter Mbotho on 2023-07-14.
//

#include "lang/operations.h"

#include "lang/builtins.h"
#include "lang/capture.h"
#include "lang/flag.h"
#include "lang/scope.h"
#include "lang/strings.h"
#include "lang/visitor.h"
#include "macro.h"

typedef struct {
    Log *L;
    MemPool *pool;
    StrPool *strings;
    Env *env;
    union {
        struct {
            bool isComptimeContext;
            AstNode *currentClosure;
        };
        struct {
            bool isComptimeContext;
            AstNode *currentClosure;
        } stack;
    };
} BindContext;

static cstring getAliasName(const AstNode *node)
{
    if (!hasFlag(node, Native))
        return NULL;
    const AstNode *alias = findAttribute(node, S_alias);

    if (alias == NULL)
        return NULL;

    const AstNode *name = findAttributeArgument(alias, S_name);

    return (nodeIs(name, StringLit)) ? name->stringLiteral.value : NULL;
}

static void defineDeclaration_(Env *env, Log *L, cstring name, AstNode *node)
{
    if (nodeIs(node, FuncDecl)) {
        defineFunctionDecl(env, L, name, node);
    }
    else
        defineForwardDeclarable(env, L, name, node);
}

static void defineDeclaration(BindContext *ctx, cstring name, AstNode *node)
{
    defineDeclaration_(ctx->env, ctx->L, name, node);
    cstring alias = getAliasName(node);
    if (alias && alias != name) {
        defineDeclaration(ctx, alias, node);
    }
}

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
            if (func == NULL) {
                logError(ctx->L,
                         &base->loc,
                         "'{s}' keyword must be used inside a member function",
                         (FormatArg[]){{.s = keyword}});
                return;
            }

            AstNode *parent =
                findEnclosingClassOrStruct(ctx->env, NULL, keyword, NULL);
            base->pathElement.enclosure = parent;
            base->flags |= (func->flags & flgConst);
            if (parent == NULL) {
                logError(ctx->L,
                         &base->loc,
                         "'{s}' keyword must be used inside a member function",
                         (FormatArg[]){{.s = keyword}});
                return;
            }

            if (keyword == S_super) {
                if (!nodeIs(parent, ClassDecl)) {
                    logError(ctx->L,
                             &base->loc,
                             "keyword 'super' can only be used inside a class "
                             "member function",
                             (FormatArg[]){{.s = keyword}});
                    return;
                }
                if (parent->classDecl.base == NULL) {
                    logError(ctx->L,
                             &base->loc,
                             "keyword 'super' can only be used within a class "
                             "which extends a base class",
                             NULL);
                    return;
                }
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

    if (!nodeIs(node->parentScope, StructDecl) &&
        !nodeIs(node->parentScope, ClassDecl)) //
    {
        defineDeclaration(ctx, getDeclarationName(node), node);
    }

    pushScope(ctx->env, node);
    astVisitManyNodes(visitor, node->genericDecl.params);
    astVisit(visitor, node->genericDecl.decl);
    popScope(ctx->env);
}

void bindDefine(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    AstNode *name = node->define.names;

    astVisit(visitor, node->define.type);

    if (node->define.container) {
        defineSymbol(
            ctx->env, ctx->L, node->define.container->ident.value, node);
        pushScope(ctx->env, node);
    }

    for (; name; name = name->next) {
        name->flags |= flgDefine;
        defineSymbol(ctx->env, ctx->L, name->ident.value, name);
        if (name->ident.alias && name->ident.alias != name->ident.value)
            defineSymbol(ctx->env, ctx->L, name->ident.alias, name);
    }

    if (node->define.container)
        popScope(ctx->env);
}

void bindImportDecl(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    const Type *exports = node->type;

    if (node->import.entities == NULL) {
        AstNode *alias = node->import.alias;
        cstring name = alias ? alias->ident.value : exports->name;
        defineSymbol(ctx->env, ctx->L, name, node);
    }
    else {
        AstNode *entity = node->import.entities;
        for (; entity; entity = entity->next) {
            defineSymbol(ctx->env,
                         ctx->L,
                         entity->importEntity.alias
                             ?: entity->importEntity.name,
                         entity->importEntity.target);
        }
    }
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
    AstNode *parent = node->parentScope;
    BindContext *ctx = getAstVisitorContext(visitor);
    if (nodeIs(parent, Program))
        defineDeclaration(ctx, node->funcDecl.name, node);

    pushScope(ctx->env, node);

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

    for (; name; name = name->next)
        defineDeclaration(ctx, name->ident.value, node);
}

void bindTypeDecl(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);

    astVisit(visitor, node->typeDecl.aliased);

    defineDeclaration(ctx, node->typeDecl.name, node);
}

void bindUnionDecl(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);

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
            option->enumOption.value =
                makeAstNode(ctx->pool,
                            &option->loc,
                            &(AstNode){.tag = astIntegerLit,
                                       .intLiteral.value = nextValue++});
            continue;
        }

        AstNode *value = option->enumOption.value;
        if (nodeIs(value, IntegerLit)) {
            nextValue = option->enumOption.value->intLiteral.value + 1;
            continue;
        }

        TODO("support paths when eval is implemented");
    }
    node->enumDecl.len = i;
    popScope(ctx->env);

    defineDeclaration(ctx, node->typeDecl.name, node);
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

    if (nodeIs(node, ClassDecl))
        astVisit(visitor, node->classDecl.base);

    astVisitManyNodes(visitor, node->structDecl.implements);

    pushScope(ctx->env, node);
    if (nodeIs(node, ClassDecl) || (!isBuiltinsInitialized()))
        defineSymbol(ctx->env, ctx->L, S_This, node);

    for (; member; member = member->next) {
        // functions will be visited later
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
        else
            astVisit(visitor, member);
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

    defineDeclaration(ctx, node->structDecl.name, node);
}

void bindInterfaceDecl(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    AstNode *member = node->interfaceDecl.members;

    pushScope(ctx->env, node);

    for (; member; member = member->next) {
        // functions will be visited later
        astVisit(visitor, member);
    }

    popScope(ctx->env);

    defineDeclaration(ctx, node->interfaceDecl.name, node);
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
    AstNode *decl = node->program.decls;

    astVisitManyNodes(visitor, node->program.top);

    for (; decl; decl = decl->next) {
        astVisit(visitor, decl);
    }
}

void withParentScope(Visitor func, AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    if (ctx->env->scope) {
        node->parentScope = ctx->env->scope->node;
    }

    bool isComptime = hasFlag(node, Comptime);
    __typeof(ctx->stack) stack = ctx->stack;

    if (!ctx->isComptimeContext && isComptime)
        ctx->isComptimeContext = isComptime;
    func(visitor, node);
    ctx->stack = stack;
}

AstNode *bindAst(CompilerDriver *driver, AstNode *node)
{
    Env env;
    environmentInit(&env, node);
    BindContext context = {.env = &env, .L = driver->L, .pool = &driver->pool};

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context, {
        [astProgram] = bindProgram,
        [astDefine] = bindDefine,
        [astImportDecl] = bindImportDecl,
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
    environmentFree(&env);

    return node;
}
