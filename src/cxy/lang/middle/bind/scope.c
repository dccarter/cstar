//
// Created by Carter on 2023-03-31.
//

#include "lang/middle/scope.h"

#include "lang/frontend/flag.h"

#include "core/alloc.h"
#include "lang/middle/builtins.h"

#include <string.h>

static inline bool compareSymbols(const void *lhs, const void *rhs)
{
    return ((const Symbol *)lhs)->name == ((const Symbol *)rhs)->name;
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
        memset(scope, 0, sizeof *scope);
        free(scope);
        scope = next;
    }
}

static u64 levenshteinDistance(const char *lhs, const char *rhs, u64 minDist)
{
    if (!lhs[0])
        return strlen(rhs);
    if (!rhs[0])
        return strlen(lhs);

    if (lhs[0] == rhs[0])
        return levenshteinDistance(lhs + 1, rhs + 1, minDist);

    if (minDist == 0)
        return 1;

    u64 a = levenshteinDistance(lhs + 1, rhs, minDist - 1);
    u64 b = levenshteinDistance(lhs, rhs + 1, minDist - 1);
    u64 c = levenshteinDistance(lhs + 1, rhs + 1, minDist - 1);

    u64 min = MIN(a, b);

    return 1 + MIN(c, min);
}

void suggestSimilarSymbol(const Env *env, Log *L, const char *name)
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
    if (isIgnoreVar(name))
        return false;

    Symbol symbol = {.name = name, .node = node, .index = 0};
    u32 hash = hashPtr(hashInit(), name);
    bool wasInserted = insertInHashTable(
        &env->scope->symbols, &symbol, hash, sizeof(Symbol), compareSymbols);
    if (!wasInserted && L) {
        logError(L,
                 &node->loc,
                 "symbol '{s}' already defined in current scope",
                 (FormatArg[]){{.s = name}});
        const Symbol *prev = findInHashTable(&env->scope->symbols,
                                             &symbol,
                                             hash,
                                             sizeof(Symbol),
                                             compareSymbols);
        csAssert0(prev);
        logNote(L, &prev->node->loc, "previously declared here", NULL);
    }

    return wasInserted;
}

bool defineForwardDeclarable(Env *env, Log *L, const char *name, AstNode *node)
{
    csAssert0(env->scope);
    if (isIgnoreVar(name))
        return true;

    Symbol symbol = {.name = name, .node = node};
    u32 hash = hashPtr(hashInit(), name);
    bool wasInserted = insertInHashTable(
        &env->scope->symbols, &symbol, hash, sizeof(Symbol), compareSymbols);

    if (!wasInserted) {
        Symbol *prev = findInHashTable(&env->scope->symbols,
                                       &symbol,
                                       hash,
                                       sizeof(Symbol),
                                       compareSymbols);
        if (!hasFlag(prev->node, ForwardDecl)) {
            if (L) {
                logError(L,
                         &node->loc,
                         "symbol '{s}' already defined in current scope",
                         (FormatArg[]){{.s = name}});
                logNote(L, &prev->node->loc, "previously declared here", NULL);
            }
            return false;
        }
        setForwardDeclDefinition(prev->node, node);
        prev->node = node;
    }

    return true;
}

void updateSymbol(Env *env, const char *name, AstNode *node)
{
    csAssert0(env->scope);
    if (isIgnoreVar(name))
        return;

    Symbol symbol = {.name = name, .node = node};
    u32 hash = hashPtr(hashInit(), name);
    bool wasInserted = insertInHashTable(
        &env->scope->symbols, &symbol, hash, sizeof(Symbol), compareSymbols);
    if (!wasInserted) {
        Symbol *prev = findInHashTable(&env->scope->symbols,
                                       &symbol,
                                       hash,
                                       sizeof(Symbol),
                                       compareSymbols);
        prev->node = node;
    }
}

void defineFunctionDecl(Env *env, Log *L, const char *name, AstNode *node)
{
    csAssert0(env->scope);
    if (isIgnoreVar(name))
        return;

    Symbol symbol = {.name = name, .node = node};
    u32 hash = hashPtr(hashInit(), name);
    bool wasInserted = insertInHashTable(
        &env->scope->symbols, &symbol, hash, sizeof(Symbol), compareSymbols);

    Symbol *sym = findInHashTable(
        &env->scope->symbols, &symbol, hash, sizeof(Symbol), compareSymbols);
    csAssert0(sym);

    if (!wasInserted && L && !nodeIs(sym->node, FuncDecl)) {
        logError(L,
                 &node->loc,
                 "symbol '{s}' already defined in current scope",
                 (FormatArg[]){{.s = name}});

        logNote(L, &sym->node->loc, "previously declared here", NULL);
    }
    else if (!wasInserted) {
        node->funcDecl.index = sym->index;
        sym->last->list.link = node;
        sym->last = node;
    }
    else {
        sym->last = node;
    }
    node->list.first = sym->node;

    sym->index++;
}

AstNode *findSymbol(const Env *env,
                    Log *L,
                    const char *name,
                    const FileLoc *loc)
{
    u32 hash = hashPtr(hashInit(), name);
    for (Scope *scope = env->scope; scope; scope = scope->prev) {
        Symbol *symbol = findInHashTable(&scope->symbols,
                                         &(Symbol){.name = name},
                                         hash,
                                         sizeof(Symbol),
                                         compareSymbols);
        if (symbol)
            return symbol->node;
    }

    if (isBuiltinsInitialized()) {
        AstNode *node = findBuiltinDecl(name);
        if (node)
            return node;
    }

    if (L) {
        logError(L, loc, "undefined symbol '{s}'", (FormatArg[]){{.s = name}});
        suggestSimilarSymbol(env, L, name);
    }

    return NULL;
}

void environmentDump(const Env *env, const char *name)
{
    if (env) {
        for (Scope *scope = env->scope; scope; scope = scope->prev) {
            Symbol *symbols = scope->symbols.elems;
            for (u32 i = 0; i < scope->symbols.capacity; i++) {
                if (!isBucketOccupied(&scope->symbols, i))
                    continue;
                printf("%s %p[%p] -> %s\n",
                       name ?: "null",
                       env,
                       scope,
                       symbols[i].name);
            }
        }
    }
}

static inline AstNode *findEnclosingScope(Env *env,
                                          Log *L,
                                          const char *keyword,
                                          const char *context,
                                          AstTag firstTag,
                                          AstTag secondTag,
                                          AstTag thirdTag,
                                          const FileLoc *loc)
{
    for (Scope *scope = env->scope; scope; scope = scope->prev) {
        if (scope->node->tag == firstTag ||  //
            scope->node->tag == secondTag || //
            scope->node->tag == thirdTag)
            return scope->node;
    }
    if (L) {
        logError(L,
                 loc,
                 "use of '{$}{s}{$}' outside of a {s}",
                 (FormatArg[]){
                     {.style = keywordStyle},
                     {.s = keyword},
                     {.style = resetStyle},
                     {.s = context},
                 });
    }
    return NULL;
}

AstNode *findEnclosingLoop(Env *env,
                           Log *L,
                           const char *keyword,
                           const FileLoc *loc)
{
    return findEnclosingScope(
        env, L, keyword, "loop", astWhileStmt, astForStmt, astError, loc);
}

AstNode *findEnclosingLoopOrSwitch(Env *env,
                                   Log *L,
                                   const char *keyword,
                                   const FileLoc *loc)
{
    return findEnclosingScope(env,
                              L,
                              keyword,
                              "loop or switch",
                              astWhileStmt,
                              astForStmt,
                              astSwitchStmt,
                              loc);
}

AstNode *findEnclosingFunctionOrClosure(Env *env, Log *L, const FileLoc *loc)
{
    return findEnclosingScope(env,
                              L,
                              "return",
                              "function",
                              astClosureExpr,
                              astFuncDecl,
                              astError,
                              loc);
}

AstNode *findEnclosingFunction(Env *env,
                               Log *L,
                               cstring keyword,
                               const FileLoc *loc)
{
    return findEnclosingScope(
        env, L, keyword, "function", astFuncDecl, astFuncDecl, astError, loc);
}

AstNode *findEnclosingStruct(Env *env,
                             Log *L,
                             cstring keyword,
                             const FileLoc *loc)
{
    return findEnclosingScope(
        env, L, keyword, "struct", astStructDecl, astStructDecl, astError, loc);
}

AstNode *findEnclosingClass(Env *env,
                            Log *L,
                            cstring keyword,
                            const FileLoc *loc)
{
    return findEnclosingScope(
        env, L, keyword, "class", astClassDecl, astClassDecl, astError, loc);
}

AstNode *findEnclosingClassOrStruct(Env *env,
                                    Log *L,
                                    cstring keyword,
                                    const FileLoc *loc)
{
    return findEnclosingScope(env,
                              L,
                              keyword,
                              "class or struct",
                              astStructDecl,
                              astClassDecl,
                              astError,
                              loc);
}

AstNode *findEnclosingBlock(Env *env,
                            cstring keyword,
                            Log *L,
                            const FileLoc *loc)
{
    return findEnclosingScope(
        env, L, keyword, "block", astBlockStmt, astBlockStmt, astError, loc);
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
    clearHashTable(&env->scope->symbols);
    env->scope = env->scope->prev;
}

void environmentInit(Env *env, AstNode *node)
{
    env->first = newScope(NULL);
    env->scope = NULL;
    pushScope(env, node);
}

void environmentFree(Env *env)
{
    if (env)
        freeScopes(env->first);
}
