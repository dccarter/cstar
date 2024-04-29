//
// Created by Carter Mbotho on 2024-04-26.
//

#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include "driver.h"

AstNode *importCHeader(CompilerDriver *driver,
                       const AstNode *node,
                       cstring name);

#ifdef __cplusplus
}
#endif