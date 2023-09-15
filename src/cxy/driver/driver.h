
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
} CompilerDriver;

typedef struct {
    cstring name;
    cstring data;
    u64 len;
    u64 mtime;
} EmbeddedSource;

cstring getFilenameWithoutDirs(cstring fileName);
char *getGeneratedPath(const Options *options,
                       cstring dir,
                       cstring filePath,
                       cstring ext);
void makeDirectoryForPath(CompilerDriver *driver, cstring path);
bool initCompilerDriver(CompilerDriver *compiler, Log *log);
bool compileFile(const char *fileName, CompilerDriver *driver);
bool generateBuiltinSources(CompilerDriver *driver);

bool compileString(CompilerDriver *driver,
                   cstring source,
                   u64 size,
                   cstring filename);
const Type *compileModule(CompilerDriver *driver,
                          const AstNode *source,
                          AstNode *entities);