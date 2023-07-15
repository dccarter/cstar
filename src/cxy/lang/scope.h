//
// Created by Carter on 2023-03-31.
//
#pragma once

#include "core/htable.h"
#include "lang/ast.h"

typedef struct Symbol {
    u64 index;
    const char *name;
    AstNode *node;
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

void updateSymbol(Env *env, const char *name, AstNode *node);
void defineFunctionDecl(Env *env, Log *L, const char *name, AstNode *node);

AstNode *findSymbol(const Env *env,
                    Log *L,
                    const char *name,
                    const FileLoc *loc);

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

Env *getBuiltinEnv(void);