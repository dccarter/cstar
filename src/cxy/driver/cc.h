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

#ifdef __cplusplus
extern "C" {
#endif

void addNativeSourceFile(CompilerDriver *driver,
                         cstring cxySource,
                         cstring source);
void addLinkLibrary(CompilerDriver *driver, cstring library);

#ifdef __cplusplus
}
#endif