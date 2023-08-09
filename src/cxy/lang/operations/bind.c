//
// Created by Carter Mbotho on 2023-07-14.
//

#include "lang/operations.h"

#include "lang/flag.h"
#include "lang/scope.h"
#include "lang/strings.h"
#include "lang/visitor.h"

typedef struct {
    Log *L;
    MemPool *pool;
    StrPool *strings;
    Env *env;
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
        defineSymbol(env, L, name, node);
}

static void defineDeclaration(BindContext *ctx, cstring name, AstNode *node)
{
    defineDeclaration_(ctx->env, ctx->L, name, node);
    cstring alias = getAliasName(node);
    if (alias && alias != name) {
        defineDeclaration(ctx, alias, node);
    }
}

static bool isCallableDecl(AstNode *node)
{
    return nodeIs(node, FuncDecl) || nodeIs(node, MacroDecl) ||
           (nodeIs(node, GenericDecl) &&
            nodeIs(node->genericDecl.decl, FuncDecl));
}

static AstNode *resolvePathBaseUpChain(BindContext *ctx, AstNode *path)
{
    AstNode *root = path->path.elements;
    AstNode *parent = findEnclosingStruct(ctx->env, NULL, NULL, NULL);
    if (parent == NULL || parent->structDecl.base == NULL ||
        !nodeIs(parent->structDecl.base, Path)) //
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

    AstNode *base = resolvePath(parent->structDecl.base);

    // lookup symbol upstream
    for (u64 i = 1; isStructDeclaration(base);
         base = resolvePath(underlyingDeclaration(base)->structDecl.base),
             i++) {
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
        base->pathElement.resolvesTo = resolvePathBaseUpChain(ctx, node);
    }
    else {
        cstring keyword = base->pathElement.name;
        if (keyword == S_This) {
            base->pathElement.enclosure =
                findEnclosingStruct(ctx->env, ctx->L, keyword, &base->loc);
            if (base->pathElement.enclosure == NULL)
                return;
        }
        else {
            base->pathElement.enclosure =
                findEnclosingFunction(ctx->env, ctx->L, keyword, &base->loc);
            if (base->pathElement.enclosure == NULL)
                return;

            AstNode *parent = getParentScope(base->pathElement.enclosure);

            if (!nodeIs(parent, StructDecl)) {
                logError(
                    ctx->L,
                    &base->loc,
                    "keyword '{s}' can only be used inside a member function",
                    (FormatArg[]){{.s = keyword}});
                return;
            }

            if (keyword == S_super && parent->structDecl.base == NULL) {
                logError(ctx->L,
                         &base->loc,
                         "keyword 'super' can only be used within a struct "
                         "which extends a base struct",
                         NULL);
                return;
            }
        }
    }

    for (AstNode *elem = base; elem; elem = elem->next)
        astVisitManyNodes(visitor, elem->pathElement.args);
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
    if (!nodeIs(node->parentScope, StructDecl))
        defineDeclaration(ctx, getDeclarationName(node), node);
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
        defineSymbol(
            ctx->env, ctx->L, name->ident.alias ?: name->ident.value, name);
    }

    if (node->define.container)
        popScope(ctx->env);
}

void bindImportDecl(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    AstNode *exports = node->import.exports;

    if (node->import.entities == NULL) {
        AstNode *alias = node->import.alias;
        cstring name = alias ? alias->ident.value : exports->moduleDecl.name;
        defineSymbol(ctx->env, ctx->L, name, exports);
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
    astVisit(visitor, node->funcParam.type);
    astVisit(visitor, node->funcParam.def);
    defineSymbol(ctx->env, ctx->L, node->funcParam.name, node);
}

void bindFunctionDecl(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);

    pushScope(ctx->env, node);

    astVisit(visitor, node->funcDecl.signature->ret);
    astVisitManyNodes(visitor, node->funcDecl.signature->params);
    astVisit(visitor, node->funcDecl.body);

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

    defineDeclaration(ctx, node->unionDecl.name, node);
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
    astVisitManyNodes(visitor, node->enumDecl.options);
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

void bindStructDecl(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    AstNode *member = node->structDecl.members;

    astVisit(visitor, node->structDecl.base);
    astVisitManyNodes(visitor, node->structDecl.implements);

    pushScope(ctx->env, node);
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
    astVisit(visitor, node->closureExpr.body);
    popScope(ctx->env);
}

void bindDeferStmt(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);

    astVisit(visitor, node->deferStmt.expr);
    node->deferStmt.block = findEnclosingBlock(ctx->env, ctx->L, &node->loc);
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
    astVisit(visitor, node->forStmt.var);
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

void bindCaseStmt(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    pushScope(ctx->env, node);

    astVisit(visitor, node->caseStmt.match);
    astVisit(visitor, node->caseStmt.body);

    popScope(ctx->env);
}

void bindProgram(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    AstNode *decl = node->program.decls;

    astVisit(visitor, node->program.top);

    for (; decl; decl = decl->next) {
        if (isCallableDecl(decl)) {
            defineDeclaration(ctx, getDeclarationName(decl), decl);
        }
        astVisit(visitor, decl);
    }
}

void withParentScope(Visitor func, AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    if (ctx->env->scope) {
        node->parentScope = ctx->env->scope->node;
    }
    func(visitor, node);
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
        [astFuncDecl] = bindFunctionDecl,
        [astMacroDecl] = bindMacroDecl,
        [astFuncParam] = bindFuncParam,
        [astVarDecl] = bindVarDecl,
        [astTypeDecl] = bindTypeDecl,
        [astUnionDecl] = bindUnionDecl,
        [astEnumOption] = bindEnumOption,
        [astEnumDecl] = bindEnumDecl,
        [astStructField] = bindStructField,
        [astStructDecl] = bindStructDecl,
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
        [astCaseStmt] = bindCaseStmt
    }, .fallback = astVisitFallbackVisitAll, .dispatch = withParentScope);
    // clang-format on

    astVisit(&visitor, node);
    environmentFree(&env);

    return node;
}
