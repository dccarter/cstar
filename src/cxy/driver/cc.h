/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-23
 */

#pragma once

#include "driver.h"

void compileCSourceFile(CompilerDriver *driver, const char *sourceFile);
bool createSourceFile(CompilerDriver *driver,
                      const FormatState *code,
                      cstring *filePath,
                      u64 flags);
bool generateAllBuiltinSources(CompilerDriver *driver);