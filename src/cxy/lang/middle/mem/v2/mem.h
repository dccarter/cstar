//
// Created by Carter Mbotho on 2024-12-08.
//

#pragma once

#include <core/hmap.h>
#include <core/strpool.h>
#include <lang/frontend/visitor.h>
#include <lang/middle/scope.h>

#ifdef __cplusplus
extern "C" {
#endif

void manageMemoryV2(CompilerDriver *cc, AstNode *node);
void memoryFinalize(CompilerDriver *cc, AstNode *node);

#ifdef __cplusplus
}
#endif
