
#pragma once

#include <driver/options.h>

#include <core/strpool.h>
#include <lang/scope.h>

typedef struct CompilerDriver {
    Options options;
    MemPool memPool;
    StrPool strPool;
    HashTable moduleCache;
    Log *L;
    TypeTable *typeTable;
    Env *builtins;
} CompilerDriver;

void makeDirectoryForPath(CompilerDriver *driver, cstring path);
bool initCompilerDriver(CompilerDriver *compiler, Log *log);
bool compileSource(const char *fileName, CompilerDriver *driver);
bool generateBuiltinSources(CompilerDriver *driver);

bool compileSourceString(CompilerDriver *driver,
                         cstring source,
                         u64 size,
                         cstring filename);
AstNode *compileModule(CompilerDriver *driver,
                       const AstNode *source,
                       const AstNode *entities);