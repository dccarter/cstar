//
// Created by Carter on 2023-03-31.
//

#include <core/htable.h>
#include <lang/ast.h>

typedef struct {
    const char *name;
    AstNode *declSite;
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
};

void environmentInit(Env *env);
void environmentFree(Env *env);

bool defineSymbol(Env *env, Log *L, const char *name, AstNode *node);
AstNode *findSymbol(Env *env, Log *L, const char *name, const FileLoc *loc);
AstNode *findSymbolAndScope(
    Env *env, Log *L, const char *name, const FileLoc *loc, Scope **scope);
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