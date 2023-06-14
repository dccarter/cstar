/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-23
 */

#include "cc.h"

#include "lang/ast.h"

#include "c.inc.h"
#include "coro.inc.h"
#include "evloop.inc.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

static struct {
    cstring name;
    cstring data;
    u64 len;
} sGeneratedFiles[] = {
    {.name = "coro.c", .data = CXY_CORO_CODE, .len = CXY_CORO_CODE_SIZE},
    {.name = "evloop.c",
     .data = CXY_EV_LOOP_CODE,
     .len = CXY_EV_LOOP_CODE_SIZE},
    {.name = "c.c", .data = CXY_C_WRAPPERS_CODE, CXY_C_WRAPPERS_CODE_SIZE}};

bool generateBuiltinSources(CompilerDriver *driver)
{
    const Options *options = &driver->options;

    for (u64 i = 0; i < sizeof__(sGeneratedFiles); i++) {
        FormatState state = newFormatState("", true);
        format(&state,
               "{s}/c/imports/{s}",
               (FormatArg[]){{.s = options->buildDir},
                             {.s = sGeneratedFiles[i].name}});

        cstring fname = formatStateToString(&state);
        freeFormatState(&state);

        if (access(fname, F_OK) == 0)
            continue;

        FILE *output = fopen(fname, "w");
        if (output == NULL) {
            logError(driver->L,
                     NULL,
                     "creating builtin source file '{s}' failed: '{s}'",
                     (FormatArg[]){{.s = fname}, {.s = strerror(errno)}});
            free((char *)fname);
            return false;
        }

        fwrite(sGeneratedFiles[i].data, 1, sGeneratedFiles[i].len, output);
        fclose(output);
        free((char *)fname);
    }

    return true;
}

void compileCSourceFile(CompilerDriver *driver, const char *sourceFile)
{
    const Options *options = &driver->options;
    FormatState state = newFormatState("    ", true);

    if (options->output)
        makeDirectoryForPath(driver, options->output);

    format(&state,
           "cc {s} -g -o {s} -I{s}/c/imports -Wno-c2x-extensions",
           (FormatArg[]){{.s = sourceFile},
                         {.s = driver->options.output ?: "app"},
                         {.s = options->buildDir}});

    if (options->rest) {
        format(&state, " {s}", (FormatArg[]){{.s = options->rest}});
    }

    char *cmd = formatStateToString(&state);
    freeFormatState(&state);
    system(cmd);
    free(cmd);
}
