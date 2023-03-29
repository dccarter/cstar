#include "driver/options.h"
#include "core/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage()
{
    printf("cxy -- a programming language giving c steroids (ver. " CXY_VERSION
           ")\n"
           "usage: cxy [options] files...\n"
           "options:\n"
           "  -h    --help           Shows this message\n"
           "        --print-ast      Prints the AST on the standard output\n"
           "        --no-key-check  Disables key checking\n"
           "        --no-color       Disables colored output\n"
           "        --max-errors     Sets the maximum number of errors\n");
}

static inline bool checkOptionArg(int i, int argc, char **argv, Log *log)
{
    if (i + 1 >= argc) {
        logError(log,
                 NULL,
                 "missing argument for option '{s}'",
                 (FormatArg[]){{.s = argv[i]}});
        return false;
    }
    return true;
}

bool parse_options(int *argc, char **argv, Options *options, Log *log)
{
    bool status = true;
    int file_count = 0;
    for (int i = 1, n = *argc; i < n; ++i) {
        if (argv[i][0] != '-') {
            argv[++file_count] = argv[i];
            continue;
        }
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage();
            goto error;
        }
        else if (!strcmp(argv[i], "--no-color"))
            log->state->ignoreStyle = true;
        else if (!strcmp(argv[i], "--no-key-check"))
            options->noTypeCheck = true;
        else if (!strcmp(argv[i], "--print-ast"))
            options->printAst = true;
        else if (!strcmp(argv[i], "--max-errors")) {
            if (!checkOptionArg(i, n, argv, log))
                goto error;
            log->maxErrors = strtoull(argv[++i], NULL, 10);
        }
        else {
            logError(log,
                     NULL,
                     "invalid option '{s}'",
                     (FormatArg[]){{.s = argv[i]}});
            goto error;
        }
    }
    if (file_count == 0) {
        logError(log,
                 NULL,
                 "no input file, run with '--help' to display usage",
                 NULL);
        goto error;
    }
    goto exit;

error:
    status = false;
exit:
    *argc = file_count + 1;
    return status;
}
