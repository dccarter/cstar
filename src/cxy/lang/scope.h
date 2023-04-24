//
// Created by Carter on 2023-03-31.
//
#pragma once

#include "core/htable.h"
#include "lang/ast.h"

typedef struct SymbolRef {
    AstNode *node;
    struct SymbolRef *next;
} SymbolRef;

typedef struct Symbol {
    const char *name;
    SymbolRef ref;
} Symbol;

typedef struct Env Env;

typedef struct Scope {
    HashTable symbols;
    AstNode *node;
    Env *env;
    struct Scope *next, *prev;
    u64 level;
} Scope;

struct Env {
    Scope *scope;
    Scope *first;
    const struct Env *up;
};

void environmentInit(Env *env);
static inline void environmentAttachUp(Env *env, const Env *up)
{
    env->up = up;
}
static inline void environmentDetachUp(Env *env) { env->up = NULL; }
void environmentFree(Env *env);
void releaseScope(Env *env, Env *into);

bool defineSymbol(Env *env, Log *L, const char *name, AstNode *node);
AstNode *findSymbol(const Env *env,
                    Log *L,
                    const char *name,
                    const FileLoc *loc);

SymbolRef *findSymbolRef(const Env *env, const char *name);

AstNode *findSymbolAndScope(const Env *env,
                            Log *L,
                            const char *name,
                            const FileLoc *loc,
                            Scope **scope);

AstNode *findSymbolOnly(const Env *env, const char *name);
AstNode *findEnclosingLoop(Env *env,
                           Log *L,
                           const char *keyword,
                           const FileLoc *loc);
AstNode *findEnclosingFunc(Env *env, Log *L, const FileLoc *loc);
AstNode *findEnclosingBlock(Env *env, Log *L, const FileLoc *loc);
void pushScope(Env *env, AstNode *node);
void popScope(Env *env);

static inline bool isRootScope(Scope *scope)
{
    return scope && scope->prev == NULL;
}
static inline bool isInSameEnv(Scope *lhs, Scope *rhs)
{
    return lhs && rhs && lhs->env == rhs->env;
}

static inline bool isAncestorScope(Scope *lhs, Scope *rhs)
{
    return lhs && rhs && lhs->level < rhs->level;
}