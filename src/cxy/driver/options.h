
#pragma once

#include "core/array.h"
#include <core/utils.h>

typedef struct Log Log;
struct StrPool;

typedef enum { cmdDev, cmdBuild, cmdRun } Command;
// clang-format off
#define DUMP_OPTIONS(ff)    \
    ff(NONE)                \
    ff(JSON)                \
    ff(YAML)                \
    ff(CXY)

typedef enum {
#define ff(N) dmp##N,
    DUMP_OPTIONS(ff)
    dmpCOUNT
#undef ff
} DumpModes;

typedef enum OptimizationLevel {
    O0,
    Od = O0,
    O1,
    O2,
    O3,
    Os
} OptimizationLevel;

typedef struct CompilerDefine {
    cstring name;
    cstring value;
} CompilerDefine;

// clang-format on

typedef struct Options {
    Command cmd;
    const char *output;
    const char *libDir;
    const char *buildDir;
    const char *rest;
    DynArray cflags;
    DynArray libraries;
    DynArray librarySearchPaths;
    DynArray defines;
    bool withoutBuiltins;
    bool noPIE;
    OptimizationLevel optimizationLevel;
    struct {
        bool printIR;
        bool emitBitCode;
        bool emitAssembly;
        bool cleanAst;
        bool withLocation;
        bool withoutAttrs;
        bool withNamedEnums;
        DumpModes dumpMode;
        u64 lastStage;
    } dev;
    bool progress;
} Options;

/// Parse command-line options, and remove those parsed options from the
/// argument list. After parsing, `argc` and `argv` are modified to only
/// contain the arguments that were not parsed.
bool parseCommandLineOptions(int *argc,
                             char **argv,
                             struct StrPool *strings,
                             Options *options,
                             Log *log);

void deinitCommandLineOptions(Options *options);
