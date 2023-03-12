#pragma once

#include <stdint.h>

#include "core/format.h"
#include "core/htable.h"

/*
 * The log object is used to report messages from various passes of the
 * compiler. It also caches source files, so as to print error diagnostics
 * efficiently.
 */

typedef struct {
    uint32_t row, col;
    size_t byteOffset;
} FilePos;

typedef struct {
    const char *fileName;
    FilePos begin, end;
} FileLoc;

typedef struct Log {
    HashTable fileCache;
    FormatState *state;
    size_t errorCount;
    size_t warningCount;
    size_t maxErrors;
    bool showDiagnostics;
} Log;

Log newLog(FormatState *);
void freeLog(Log *);

void logError(Log *, const FileLoc *, const char *, const FormatArg *);
void logWarning(Log *, const FileLoc *, const char *, const FormatArg *);
void logNote(Log *, const FileLoc *, const char *, const FormatArg *);
