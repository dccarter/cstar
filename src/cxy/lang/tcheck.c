//
// Created by Carter on 2023-03-28.
//

#include "tcheck.h"
#include "ttable.h"

#include "core/alloc.h"
#include "core/utils.h"

#include <string.h>

typedef struct {
    const char *name;
    AstNode *declSite;
} Symbol;

typedef struct Scope {
    HashTable symbols;
    AstNode *node;
    struct Scope *next, *prev;
} Scope;

typedef struct {
    Log *L;
    MemPool *pool;
    TypeTable *typeTable;
    struct {
        Scope *first, *scope;
    } env;
} CheckerContext;

static inline bool compareSymbols(const void *lhs, const void *rhs)
{
    return !strcmp(((const Symbol *)lhs)->name, ((const Symbol *)rhs)->name);
}

static Scope *newScope(Scope *prev)
{
    Scope *next = mallocOrDie(sizeof(Scope));
    next->prev = prev;
    next->next = NULL;
    next->symbols = newHashTable(sizeof(Symbol));
    if (prev)
        prev->next = next;

    return next;
}

static void freeScopes(Scope *scope)
{
    while (scope) {
        Scope *next = scope->next;
        freeHashTable(&scope->symbols);
        free(scope);
        scope = next;
    }
}

static void defineSymbol(CheckerContext *context,
                         const char *name,
                         AstNode *node)
{
    csAssert0(context->env.scope);
    if (name[0] == '_')
        return;

    Symbol symbol = {.name = name, .declSite = node};
    u32 hash = hashStr(hashInit(), name);
    bool wasInserted = insertInHashTable(&context->env.scope->symbols,
                                         &symbol,
                                         hash,
                                         sizeof(Symbol),
                                         compareSymbols);
    if (!wasInserted) {
        logError(context->L,
                 &node->loc,
                 "symbol {s} already defined",
                 (FormatArg[]){{.s = name}});
        const Symbol *prev = findInHashTable(&context->env.scope->symbols,
                                             &symbol,
                                             hash,
                                             sizeof(Symbol),
                                             compareSymbols);
        csAssert0(prev);
        logNote(
            context->L, &prev->declSite->loc, "previously declared here", NULL);
    }
}

static u64 levenshteinDistance(const char *lhs, const char *rhs, u64 minDist)
{
    if (!lhs)
        return strlen(rhs);
    if (!rhs)
        return strlen(lhs);

    if (lhs[0] == rhs[0])
        return levenshteinDistance(lhs + 1, rhs + 1, minDist);

    if (minDist == 0)
        return 1;

    u64 a = levenshteinDistance(lhs + 1, rhs, minDist - 1);
    u64 b = levenshteinDistance(lhs, rhs + 1, minDist - 1);
    u64 c = levenshteinDistance(lhs + 1, rhs + 1, minDist - 1);

    u64 min = MIN(a, b);

    return MIN(min, c);
}

static void suggestSimilarSymbol(CheckerContext *ctx, const char *name)
{
    u64 minDist = 2;

    if (strlen(name) <= minDist)
        return;

    const char *similar = NULL;
    for (Scope *scope = ctx->env.scope; scope; scope = scope->prev) {
        Symbol *symbols = scope->symbols.elems;
        for (u32 i = 0; i < scope->symbols.capacity; i++) {
            if (!isBucketOccupied(&scope->symbols, i))
                continue;
            u64 dist = levenshteinDistance(name, symbols[i].name, minDist);
            if (dist < minDist) {
                minDist = dist;
                similar = symbols[i].name;
            }
        }
    }

    if (similar) {
        logNote(
            ctx->L, NULL, "did you mean '{s}'", (FormatArg[]){{.s = similar}});
    }
}

static AstNode *findSymbol(CheckerContext *ctx,
                           const char *name,
                           const FileLoc *loc)
{
    u32 hash = hashStr(hashInit(), name);
    for (Scope *scope = ctx->env.scope; scope; scope = scope->prev) {
        Symbol *symbol = findInHashTable(&scope->symbols,
                                         &(Symbol){.name = name},
                                         hash,
                                         sizeof(Symbol),
                                         compareSymbols);
        if (symbol)
            return symbol->declSite;
    }

    logError(ctx->L, loc, "undefined symbol '{s}'", (FormatArg[]){{.s = name}});
    suggestSimilarSymbol(ctx, name);
    return NULL;
}

static inline AstNode *findEnclosingScope(CheckerContext *ctx,
                                          const char *keyword,
                                          const char *context,
                                          AstTag firstTag,
                                          AstTag secondTag,
                                          const FileLoc *loc)
{
    for (Scope *scope = ctx->env.scope; scope; scope = scope->prev) {
        if (scope->node->tag == firstTag || scope->node->tag == secondTag)
            return scope->node;
    }

    logError(ctx->L,
             loc,
             "use of '{$}{s}{$}' outside of a {s}",
             (FormatArg[]){
                 {.style = keywordStyle},
                 {.s = keyword},
                 {.style = resetStyle},
                 {.s = context},
             });
    return NULL;
}

static inline AstNode *findEnclosingLoop(CheckerContext *ctx,
                                         const char *keyword,
                                         const FileLoc *loc)
{
    return findEnclosingScope(
        ctx, keyword, "loop", astWhileStmt, astForStmt, loc);
}

static inline AstNode *findEnclosingFunc(CheckerContext *ctx,
                                         const FileLoc *loc)
{
    return findEnclosingScope(
        ctx, "return", "function", astClosureExpr, astFuncDecl, loc);
}

static void pushScope(CheckerContext *ctx, AstNode *node)
{
    if (!ctx->env.scope)
        ctx->env.scope = ctx->env.first;
    else if (ctx->env.scope->next)
        ctx->env.scope = ctx->env.scope->next;
    else
        ctx->env.scope = newScope(ctx->env.scope);
    ctx->env.scope->node = node;
    clearHashTable(&ctx->env.scope->symbols);
}

static inline void popScope(CheckerContext *ctx)
{
    csAssert0(ctx->env.scope);
    ctx->env.scope = ctx->env.scope->prev;
}

static inline const Type *evalType(AstVisitor *visitor, AstNode *node)
{
    astVisit(visitor, node);
    return node->type;
}

static inline u64 checkMany(AstVisitor *visitor, AstNode *node)
{
    u64 i = 0;
    for (; node; node = node->next, i++)
        astVisit(visitor, node);

    return i;
}

static void checkProgram(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    pushScope(ctx, node);
    checkMany(visitor, node->program.decls);
}

static void checkFallback(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    node->type = makeVoidType(ctx->typeTable);
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
        if (part->type == makeErrorType(ctx->typeTable)) {
            node->type = makeErrorType(ctx->typeTable);
        }
    }

    node->type = makeStringType(ctx->typeTable);
}

static void checkFuncDecl(AstVisitor *visitor, AstNode *node) {}

static void checkVarDecl(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    node->type = node->varDecl.type ? evalType(visitor, node->varDecl.type)
                                    : makeAutoType(ctx->typeTable);
    const Type *value = NULL;
    if (node->varDecl.init) {
        value = evalType(visitor, node->varDecl.init);
        if (!isTypeAssignableFrom(ctx->typeTable, node->type, value)) {
            logError(ctx->L,
                     &node->varDecl.init->loc,
                     "incompatible types, expecting type '{t}', got '{t}",
                     (FormatArg[]){{.t = node->type}, {.t = value}});
        }
        else {
            node->type = value;
        }
    }

    defineSymbol(ctx, node->varDecl.names->ident.value, node);
}

static void checkIdentifier(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    AstNode *symbol = findSymbol(ctx, node->ident.value, &node->loc);
    if (symbol == NULL)
        node->type = makeErrorType(ctx->typeTable);
    else
        node->type = symbol->type;
}

static void checkPathElement(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    AstNode *symbol = findSymbol(ctx, node->pathElement.name, &node->loc);
    if (symbol == NULL)
        node->type = makeErrorType(ctx->typeTable);
    else
        node->type = symbol->type;
}

static void checkPath(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    AstNode *element = node->path.elements;
    const Type *type;
    for (; element; element = element->next) {
        // TODO, change scope
        type = evalType(visitor, element);
    }
    node->type = type;
}

static void checkBinary(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    const Type *left = evalType(visitor, node->binaryExpr.lhs);
    const Type *right = evalType(visitor, node->binaryExpr.rhs);
    // TODO check compatibility
    if (isTypeAssignableFrom(ctx->typeTable, left, right)) {
        node->type = left;
    }
    else {
        node->type = makeErrorType(ctx->typeTable);
    }
}

static void checkUnary(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    const Type *operand = evalType(visitor, node->unaryExpr.operand);
    // TODO check compatibility
    node->type = operand;
}

static void checkPrimitiveType(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    node->type = makePrimitiveType(ctx->typeTable, node->primitiveType.id);
}

void typeCheck(AstNode *program, Log *L, MemPool *pool, TypeTable *typeTable)
{
    CheckerContext context = {.L = L,
                              .typeTable = typeTable,
                              .pool = pool,
                              .env = {.first = newScope(NULL)}};

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
        [astFuncDecl] = checkFuncDecl,
        [astVarDecl] = checkVarDecl,
        [astConstDecl] = checkVarDecl,
        [astIdentifier] = checkIdentifier,
        [astBinaryExpr] = checkBinary,
        [astUnaryExpr] = checkUnary,
        [astPrimitiveType] = checkPrimitiveType
    },
    .fallback = checkFallback);
    // clang-format off

    astVisit(&visitor, program);
}