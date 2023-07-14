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
    u32 index;
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
    struct Env *prev, *list;
    Scope *scope;
    Scope *first;
    cstring debug;
};

typedef struct {
    Env *first;
    Env *last;
} EnvList;

void environmentInit(Env *env, AstNode *node);
void environmentAddToList(EnvList *list, Env *env);
void setBuiltinEnvironment(Env *env);

Env *makeEnvironment_(MemPool *pool, AstNode *node, cstring debug);
#define makeEnvironment(pool, node)                                            \
    makeEnvironment_(                                                          \
        (pool), (node), __FILE_NAME__ CXY_STR(CXY_PASTE(_, __LINE__)))

Env *environmentPush(Env *this, Env *env);
Env *environmentPop(Env *env);
Env *environmentRoot(Env *env);
bool environmentFind(Env *env, Env **root);

void environmentFree(Env *env);
void environmentFreeUnusedScope(Env *env);
void environmentDump(const Env *env, const char *name);

void releaseScope(Env *env, Env *into);

bool defineSymbol(Env *env, Log *L, const char *name, AstNode *node);
SymbolRef *updateSymbol(Env *env, const char *name, AstNode *node);

SymbolRef *defineFunctionDecl(Env *env,
                              Log *L,
                              const char *name,
                              AstNode *node);

SymbolRef *getLastSymbolRef(SymbolRef *ref);
SymbolRef *getSymbolRefAt(SymbolRef *ref, u32 index);

AstNode *findSymbol(const Env *env,
                    Log *L,
                    const char *name,
                    const FileLoc *loc);

SymbolRef *findSymbolRef(const Env *env,
                         Log *L,
                         const char *name,
                         const FileLoc *loc);

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
AstNode *findEnclosingLoopOrSwitch(Env *env,
                                   Log *L,
                                   const char *keyword,
                                   const FileLoc *loc);

AstNode *findEnclosingFunc(Env *env, Log *L, const FileLoc *loc);
AstNode *findEnclosingBlock(Env *env, Log *L, const FileLoc *loc);
void pushScope(Env *env, AstNode *node);
void popScope(Env *env);
const Env *getUpperEnv(const Env *);
Env *getBuiltinEnv(void);
bool isBuiltinEnv(const Env *);

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
