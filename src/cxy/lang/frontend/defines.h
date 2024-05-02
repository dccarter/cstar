//
// Created by Carter Mbotho on 2024-03-12.
//

#pragma once

#include "core/htable.h"
#include "lang/frontend/ast.h"

#ifdef __cplusplus
extern "C" {
#endif

struct CompilerPreprocessor;

bool preprocessorOverrideDefinedMacro(struct CompilerPreprocessor *preprocessor,
                                      cstring variable,
                                      AstNode *value,
                                      AstNode **previous);

static inline bool preprocessorDefineMacro(
    struct CompilerPreprocessor *preprocessor, cstring variable, AstNode *value)
{
    return preprocessorOverrideDefinedMacro(
        preprocessor, variable, value, NULL);
}

bool preprocessorHasMacro(struct CompilerPreprocessor *preprocessor,
                          cstring variable,
                          AstNode **value);

static inline AstNode *preprocessorFindMacro(
    struct CompilerPreprocessor *preprocessor, cstring variable)
{
    AstNode *value = NULL;
    preprocessorHasMacro(preprocessor, variable, &value);
    return value;
}

#ifdef __cplusplus
}
#endif