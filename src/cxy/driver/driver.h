
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
    HashTable nativeSources;
    HashTable linkLibraries;
    cstring currentDir; // Directory that cxy was executed from
    u64 currentDirLen;
    cstring sourceDir; // Root directory for source files (derived from first
                       // compile unit)
    u64 sourceDirLen;
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
char *getGeneratedPath(CompilerDriver *driver,
                       cstring dir,
                       cstring filePath,
                       cstring ext);
void makeDirectoryForPath(CompilerDriver *driver, cstring path);
cstring getFilePathAsRelativeToCxySource(StrPool *strings,
                                         cstring cxySource,
                                         cstring file);
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