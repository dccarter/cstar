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

#include <string.h>

static bool stringCompare(const void *lhs, const void *rhs)
{
    return strcmp(*((cstring *)lhs), *((cstring *)rhs)) == 0;
}

cstring getFilePathAsRelativeToCxySource(StrPool *strings,
                                         cstring relativeTo,
                                         cstring file)
{
    csAssert0(file && relativeTo);
    if (file[0] == '/')
        return file;
    cstring relativeToFilename = strrchr(relativeTo, '/');
    if (relativeToFilename == NULL)
        return file;
    size_t len = (relativeToFilename - relativeTo) + 1, fileLen = strlen(file);
    char path[1024];
    memcpy(path, relativeTo, len);
    memcpy(&path[len], file, fileLen);

    return makeStringSized(strings, path, len + fileLen);
}

void addNativeSourceFile(CompilerDriver *driver,
                         cstring cxySource,
                         cstring source)
{
    source =
        getFilePathAsRelativeToCxySource(driver->strings, cxySource, source);
    insertInHashTable(&driver->nativeSources,
                      &source,
                      hashStr(hashInit(), source),
                      sizeof(source),
                      stringCompare);
}

void addLinkLibrary(CompilerDriver *driver, cstring lib)
{
    insertInHashTable(&driver->linkLibraries,
                      &lib,
                      hashStr(hashInit(), lib),
                      sizeof(lib),
                      stringCompare);
}
