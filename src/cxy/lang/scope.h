//
// Created by Carter on 2023-03-31.
//
#pragma once

#include "core/htable.h"
#include "lang/ast.h"

typedef struct Symbol {
    u16 index;
    const char *name;
    AstNode *node;
    AstNode *last;
} Symbol;

typedef struct Scope {
    HashTable symbols;
    AstNode *node;
    struct Scope *next, *prev;
    u64 level;
} Scope;

typedef struct Env {
    Scope *scope;
    Scope *first;
} Env;

void environmentInit(Env *env, AstNode *node);

void environmentFree(Env *env);
void environmentDump(const Env *env, const char *name);

bool defineSymbol(Env *env, Log *L, const char *name, AstNode *node);
bool defineForwardDeclarable(Env *env, Log *L, const char *name, AstNode *node);

void updateSymbol(Env *env, const char *name, AstNode *node);
void defineFunctionDecl(Env *env, Log *L, const char *name, AstNode *node);

AstNode *findSymbol(const Env *env,
                    Log *L,
                    const char *name,
                    const FileLoc *loc);

static inline AstNode *findSymbolOnly(const Env *env, const char *name)
{
    return findSymbol(env, NULL, name, NULL);
}

void suggestSimilarSymbol(const Env *env, Log *L, const char *name);

AstNode *findEnclosingLoop(Env *env,
                           Log *L,
                           const char *keyword,
                           const FileLoc *loc);

AstNode *findEnclosingLoopOrSwitch(Env *env,
                                   Log *L,
                                   const char *keyword,
                                   const FileLoc *loc);

AstNode *findEnclosingFunctionOrClosure(Env *env, Log *L, const FileLoc *loc);
AstNode *findEnclosingFunction(Env *env,
                               Log *L,
                               cstring keyword,
                               const FileLoc *loc);

AstNode *findEnclosingStruct(Env *env,
                             Log *L,
                             cstring keyword,
                             const FileLoc *loc);

AstNode *findEnclosingClass(Env *env,
                            Log *L,
                            cstring keyword,
                            const FileLoc *loc);

AstNode *findEnclosingClassOrStruct(Env *env,
                                    Log *L,
                                    cstring keyword,
                                    const FileLoc *loc);

AstNode *findEnclosingBlock(Env *env,
                            cstring keyword,
                            Log *L,
                            const FileLoc *loc);

void pushScope(Env *env, AstNode *node);

void popScope(Env *env);

Env *getBuiltinEnv(void);