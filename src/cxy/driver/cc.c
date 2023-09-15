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
#include "lang/flag.h"

#include "epilogue.h"
#include "prologue.h"
#include "runtime.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static EmbeddedSource sGeneratedFiles[] = {};
static EmbeddedSource sRuntimeSources[2];

static EmbeddedSource *getRuntimeSources()
{
    static bool initialized = false;
    if (!initialized) {
        sRuntimeSources[0] =
            (EmbeddedSource){.name = "runtime.c",
                             .data = CXY_RUNTIME_SOURCE,
                             .len = CXY_RUNTIME_SOURCE_SIZE,
                             .mtime = CXY_RUNTIME_SOURCE_MTIME};
        sRuntimeSources[1] =
            (EmbeddedSource){.name = "prologue.h",
                             .data = CXY_PROLOGUE_SOURCE,
                             .len = CXY_PROLOGUE_SOURCE_SIZE,
                             .mtime = CXY_PROLOGUE_SOURCE_MTIME};
        initialized = true;
    }

    return sRuntimeSources;
}

static bool generateEmbeddedSources(CompilerDriver *driver,
                                    cstring dir,
                                    EmbeddedSource *sources,
                                    u64 count)
{
    const Options *options = &driver->options;

    for (u64 i = 0; i < count; i++) {
        FormatState state = newFormatState("", true);
        format(&state,
               "{s}/c/{s}/{s}",
               (FormatArg[]){{.s = options->buildDir},
                             {.s = dir},
                             {.s = sources[i].name}});
        char *fname = formatStateToString(&state);
        freeFormatState(&state);

        struct stat st;
        if (stat(fname, &st) == 0) {
            u64 mtime = timespecToMicroSeconds(&st.st_mtimespec);
            if (mtime > sources[i].mtime) {
                free(fname);
                continue;
            }
        }
        else {
            makeDirectoryForPath(driver, fname);
        }

        FILE *output = fopen(fname, "w");
        if (output == NULL) {
            logError(driver->L,
                     NULL,
                     "creating builtin source file '{s}' failed: '{s}'",
                     (FormatArg[]){{.s = fname}, {.s = strerror(errno)}});
            free(fname);
            return false;
        }

        fwrite(sources[i].data, 1, sources[i].len, output);
        fclose(output);
        free(fname);
    }

    return true;
}

bool generateAllBuiltinSources(CompilerDriver *driver)
{
    if (!generateEmbeddedSources(
            driver, "runtime", getRuntimeSources(), sizeof__(sRuntimeSources)))
        return false;

    return generateEmbeddedSources(
        driver, "imports", sGeneratedFiles, sizeof__(sGeneratedFiles));
}

void compileCSourceFile(CompilerDriver *driver, const char *sourceFile)
{
    const Options *options = &driver->options;
    FormatState state = newFormatState("    ", true);

    if (options->output)
        makeDirectoryForPath(driver, options->output);

    format(&state,
           "cc {s}/c/runtime/runtime.c {s} -g -o {s} -I{s}/c -I{s}/c/imports "
           "-D__CXY_BUILD__ -Wno-c2x-extensions",
           (FormatArg[]){{.s = options->buildDir},
                         {.s = sourceFile},
                         {.s = driver->options.output ?: "app"},
                         {.s = options->buildDir},
                         {.s = options->buildDir}});

    if (options->rest) {
        format(&state, " {s}", (FormatArg[]){{.s = options->rest}});
    }

    char *cmd = formatStateToString(&state);
    freeFormatState(&state);
    system(cmd);
    free(cmd);
}

bool createSourceFile(CompilerDriver *driver,
                      const FormatState *code,
                      cstring *filePath,
                      u64 flags)
{
    const Options *options = &driver->options;
    bool isImport = (flags & flgImportedModule), isMain = (flags & flgMain);
    char *sourceFile = getGeneratedPath(
        &driver->options, isImport ? "c/imports" : "c/src", *filePath, ".c");

    makeDirectoryForPath(driver, sourceFile);

    FILE *output = fopen(sourceFile, "w");
    if (output == NULL) {
        logError(driver->L,
                 NULL,
                 "creating output file '{s}' failed, {s}",
                 (FormatArg[]){{.s = options->output}, {.s = strerror(errno)}});
        free(sourceFile);
        return false;
    }

    writeFormatState(code, output);
    fclose(output);

    if (isMain)
        *filePath = sourceFile;
    else
        free(sourceFile);

    return true;
}
