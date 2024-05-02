//
// Created by Carter on 2023-09-13.
//

#pragma once

#include <lang/frontend/ast.h>

#ifdef __cplusplus
extern "C" {
#endif

EvaluateMacro findBuiltinMacroByNode(AstNode *node);

#ifdef __cplusplus
}
#endif
