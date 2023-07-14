
#pragma once

#include <driver/options.h>
#include <driver/stats.h>

#include <core/strpool.h>
#include <lang/scope.h>

typedef struct CompilerDriver {
    Options options;
    MemPool pool;
    StrPool strPool;
    HashTable moduleCache;
    CompilerStats stats;
    Log *L;
    TypeTable *typeTable;
    Env *builtins;
} CompilerDriver;

typedef struct {
    cstring name;
    cstring data;
    u64 len;
    u64 mtime;
} EmbeddedSource;

void makeDirectoryForPath(CompilerDriver *driver, cstring path);
cstring getFilenameWithoutDirs(cstring fileName);
char *getGeneratedPath(const Options *options,
                       cstring dir,
                       cstring filePath,
                       cstring ext);
bool initCompilerDriver(CompilerDriver *compiler, Log *log);
void deInitCompilerDriver(CompilerDriver *compiler);
bool compileFile(const char *fileName, CompilerDriver *driver);
bool generateAllBuiltinSources(CompilerDriver *driver);

bool compileString(CompilerDriver *driver,
                   cstring source,
                   u64 size,
                   cstring filename);
AstNode *compileModule(CompilerDriver *driver,
                       const AstNode *source,
                       const AstNode *entities);