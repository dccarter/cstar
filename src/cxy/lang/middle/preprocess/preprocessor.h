//
// Created by Carter Mbotho on 2024-03-14.
//

#pragma once

#include "core/sb.h"
#include "driver/driver.h"
#include "lang/frontend/visitor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PreprocessorContext {
    Log *L;
    MemPool *pool;
    StrPool *strings;
    Env *env;
    CompilerPreprocessor *preprocessor;
    union {
        struct {
            FileLoc loc;
            bool resetLocation;
        };
        struct {
            FileLoc loc;
            bool resetLocation;
        } stack;
    };
} PreprocessorContext;

void preprocessorEvalUnaryExpr(PreprocessorContext *ctx, AstNode *node);
void preprocessorEvalBinaryExpr(PreprocessorContext *ctx, AstNode *node);
bool preprocessorAsBoolean(PreprocessorContext *ctx, AstNode *node);

#ifdef __cplusplus
}
#endif
