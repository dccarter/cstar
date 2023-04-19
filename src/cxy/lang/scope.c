//
// Created by Carter on 2023-03-31.
//

#include "scope.h"
#include "core/alloc.h"

#include <string.h>

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
    if (prev) {
        next->level = prev->level + 1;
        prev->next = next;
    }
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

static void suggestSimilarSymbol(const Env *env, Log *L, const char *name)
{
    u64 minDist = 2;

    if (strlen(name) <= minDist)
        return;

    const char *similar = NULL;
    for (Scope *scope = env->scope; scope; scope = scope->prev) {
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
        logNote(L, NULL, "did you mean '{s}'", (FormatArg[]){{.s = similar}});
    }
}

bool defineSymbol(Env *env, Log *L, const char *name, AstNode *node)
{
    csAssert0(env->scope);
    //    if (name[0] == '_')
    //        return false;

    Symbol symbol = {.name = name, .ref.node = node};
    u32 hash = hashStr(hashInit(), name);
    bool wasInserted = insertInHashTable(
        &env->scope->symbols, &symbol, hash, sizeof(Symbol), compareSymbols);
    if (!wasInserted) {
        logError(L,
                 &node->loc,
                 "symbol {s} already defined",
                 (FormatArg[]){{.s = name}});
        const Symbol *prev = findInHashTable(&env->scope->symbols,
                                             &symbol,
                                             hash,
                                             sizeof(Symbol),
                                             compareSymbols);
        csAssert0(prev);
        logNote(L, &prev->ref.node->loc, "previously declared here", NULL);
    }

    return wasInserted;
}

AstNode *findSymbol(const Env *env,
                    Log *L,
                    const char *name,
                    const FileLoc *loc)
{
    Scope *scope;
    return findSymbolAndScope(env, L, name, loc, &scope);
}

AstNode *findSymbolAndScope(const Env *env,
                            Log *L,
                            const char *name,
                            const FileLoc *loc,
                            Scope **outScope)
{
    u32 hash = hashStr(hashInit(), name);
    for (Scope *scope = env->scope; scope; scope = scope->prev) {
        Symbol *symbol = findInHashTable(&scope->symbols,
                                         &(Symbol){.name = name},
                                         hash,
                                         sizeof(Symbol),
                                         compareSymbols);
        *outScope = scope;
        if (symbol)
            return symbol->ref.node;
    }

    if (env->up) {
        return findSymbolAndScope(env->up, L, name, loc, outScope);
    }

    logError(L, loc, "undefined symbol '{s}'", (FormatArg[]){{.s = name}});
    suggestSimilarSymbol(env, L, name);
    return NULL;
}

SymbolRef *findSymbolRef(const Env *env, const char *name)
{
    u32 hash = hashStr(hashInit(), name);
    for (Scope *scope = env->scope; scope; scope = scope->prev) {
        Symbol *symbol = findInHashTable(&scope->symbols,
                                         &(Symbol){.name = name},
                                         hash,
                                         sizeof(Symbol),
                                         compareSymbols);
        if (symbol)
            return &symbol->ref;
    }

    if (env->up) {
        return findSymbolRef(env->up, name);
    }

    return NULL;
}

AstNode *findSymbolOnly(const Env *env, const char *name)
{
    const SymbolRef *ref = findSymbolRef(env, name);

    return ref ? ref->node : NULL;
}

static inline AstNode *findEnclosingScope(Env *env,
                                          Log *L,
                                          const char *keyword,
                                          const char *context,
                                          AstTag firstTag,
                                          AstTag secondTag,
                                          const FileLoc *loc)
{
    for (Scope *scope = env->scope; scope; scope = scope->prev) {
        if (scope->node->tag == firstTag || scope->node->tag == secondTag)
            return scope->node;
    }

    logError(L,
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

AstNode *findEnclosingLoop(Env *env,
                           Log *L,
                           const char *keyword,
                           const FileLoc *loc)
{
    return findEnclosingScope(
        env, L, keyword, "loop", astWhileStmt, astForStmt, loc);
}

AstNode *findEnclosingFunc(Env *env, Log *L, const FileLoc *loc)
{
    return findEnclosingScope(
        env, L, "return", "function", astClosureExpr, astFuncDecl, loc);
}

AstNode *findEnclosingBlock(Env *env, Log *L, const FileLoc *loc)
{
    return findEnclosingScope(
        env, L, "defer", "block", astBlockStmt, astBlockStmt, loc);
}

void pushScope(Env *env, AstNode *node)
{
    if (!env->scope)
        env->scope = env->first;
    else if (env->scope->next)
        env->scope = env->scope->next;
    else
        env->scope = newScope(env->scope);
    env->scope->node = node;
    clearHashTable(&env->scope->symbols);
}

void releaseScope(Env *env, Env *into)
{
    csAssert0(env->scope);
    into->first = env->scope;
    into->scope = env->scope;
    if (env->first != env->scope) {
        env->scope->prev->next = env->scope->next;
        into->first->prev = NULL;
        into->scope->next = NULL;
        env->scope = env->scope->next;
    }
    else {
        if (env->scope->next)
            freeScopes(env->scope->next);
        env->first = env->scope = NULL;
        into->scope->next = NULL;
    }
}

void popScope(Env *env)
{
    csAssert0(env->scope);
    env->scope = env->scope->prev;
}

void environmentInit(Env *env)
{
    env->first = newScope(NULL);
    env->scope = NULL;
}

void environmentFree(Env *env) { freeScopes(env->first); }