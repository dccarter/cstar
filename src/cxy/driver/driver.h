
#pragma once

#include <driver/options.h>

#include <core/strpool.h>
#include <lang/scope.h>

typedef struct CompilerDriver {
    Options options;
    MemPool memPool;
    StrPool strPool;
    HashTable modules;
    Log *L;
    TypeTable *typeTable;
} CompilerDriver;

void makeDirectoryForPath(CompilerDriver *driver, cstring path);
void initCompilerDriver(CompilerDriver *compiler, Log *log);
bool compileSource(const char *fileName, CompilerDriver *driver);
AstNode *compileModule(CompilerDriver *driver, const AstNode *source);