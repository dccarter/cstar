
#pragma once

#include <driver/options.h>
#include <lang/ttable.h>

typedef struct CompilerDriver {
    Options options;
    MemPool memPool;
    StrPool strPool;
    HashTable importsCache;
    Log *L;
    TypeTable *typeTable;
} CompilerDriver;

void makeDirectoryForPath(CompilerDriver *driver, cstring path);
void initCompilerDriver(CompilerDriver *compiler, Log *log);
bool compileFile(const char *fileName, CompilerDriver *driver);
