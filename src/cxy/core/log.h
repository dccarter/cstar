#pragma once

#include <stdint.h>

#include "core/format.h"
#include "core/htable.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The log object is used to report messages from various passes of the
 * compiler. It also caches what files, so as to print error diagnostics
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

static inline bool hasErrors(Log *L) { return L->errorCount > 0; }

const FileLoc *builtinLoc(void);
static inline FileLoc locSubrange(const FileLoc *start, const FileLoc *end)
{
    csAssert0(start->fileName == end->fileName);
    return (FileLoc){
        .fileName = start->fileName, .begin = start->begin, .end = end->begin};
}

static inline FileLoc *locExtend(FileLoc *dst,
                                 const FileLoc *start,
                                 const FileLoc *end)
{
    csAssert0(start->fileName == end->fileName);
    csAssert0(start->begin.byteOffset <= end->end.byteOffset);

    *dst = (FileLoc){
        .fileName = start->fileName, .begin = start->begin, .end = end->end};
    return dst;
}

static inline FileLoc *locAfter(FileLoc *dst, const FileLoc *loc)
{
    *dst = (FileLoc){
        .fileName = loc->fileName, .begin = loc->end, .end = loc->end};
    return dst;
}

#ifdef __cplusplus
}
#endif
