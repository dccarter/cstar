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

#include <string.h>

void compileCSourceFile(CompilerDriver *driver, const char *sourceFile)
{
    const Options *options = &driver->options;
    FormatState state = newFormatState("    ", true);

    if (options->output)
        makeDirectoryForPath(driver, options->output);

    format(&state,
           "cc {s} -o {s} -I{s}/c/imports -Wno-c2x-extensions",
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
