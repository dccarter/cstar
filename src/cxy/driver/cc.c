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

void cCompileToObject(CompilerDriver *driver,
                      const char *sourceFile,
                      const char *objFile)
{
    const Options *options = &driver->options;
    FormatState state = newFormatState("    ", true);
    makeDirectoryForPath(driver, objFile);

    format(&state,
           "cc -c {s} -O3 -o {s} -I{s}/c/include",
           (FormatArg[]){
               {.s = sourceFile}, {.s = objFile}, {.s = options->buildDir}});

    if (options->rest) {
        format(&state, " {s}", (FormatArg[]){{.s = options->rest}});
    }

    char *cmd = formatStateToString(&state);
    freeFormatState(&state);
    system(cmd);
    free(cmd);
}

void cLinkObjects(CompilerDriver *driver, const AstNode *program)
{
    // links object files
}
