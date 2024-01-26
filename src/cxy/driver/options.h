
#pragma once

#include "core/array.h"
#include <core/utils.h>

typedef struct Log Log;
struct StrPool;

typedef enum { cmdDev, cmdBuild } Command;

typedef struct Options {
    Command cmd;
    const char *output;
    const char *libDir;
    const char *buildDir;
    const char *rest;
    DynArray cflags;
    DynArray ldflags;
    DynArray defines;
    bool withoutBuiltins;
    struct {
        bool printAst;
        bool printIR;
        bool cleanAst;
        bool withLocation;
        bool withoutAttrs;
        bool withNamedEnums;
        bool dumpJson;
        struct {
            cstring str;
            u64 num;
        } lastStage;
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
