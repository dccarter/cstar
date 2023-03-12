
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Log Log;

typedef struct Options {
    bool printAst;
    bool noTypeCheck;
} Options;

static const Options default_options = {.printAst = false,
                                        .noTypeCheck = false};

/// Parse command-line options, and remove those parsed options from the
/// argument list. After parsing, `argc` and `argv` are modified to only
/// contain the arguments that were not parsed.
bool parse_options(int *argc, char **argv, Options *options, Log *log);


