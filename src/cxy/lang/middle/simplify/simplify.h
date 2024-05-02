//
// Created by Carter Mbotho on 2024-04-30.
//

#pragma once

#include <driver/driver.h>

#ifdef __cplusplus
extern "C" {
#endif

AstNode *simplifyDeferStatements(CompilerDriver *driver, AstNode *node);

#ifdef __cplusplus
}
#endif
