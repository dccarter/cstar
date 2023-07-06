//
// Created by Carter Mbotho on 2023-07-05.
//

#pragma once

#include <driver/driver.h>
#include <lang/ast.h>

// clang-format off
#define CXY_COMPILER_STAGES(f)                          \
    f(Parse,          1, "Parse")                       \
    f(Desugar,        2, "De-sugar")                    \
    f(NameRes,        3, "Name Resolution")             \
    f(ConstCheck,     4, "Const Check")                 \
    f(TypeCheck,      5, "Type Check")                  \
    f(Comptime,       6, "Compile Time Evaluation")     \
    f(Optimization,   7, "Optimization")                \
    f(MemoryMgmt,     8, "Memory Management")           \
    f(Codegen,        9, "Code Generation")

typedef enum {
    ccsInvalid = 0,
#define f(NAME, IDX, ...) ccs##NAME = 1 << IDX,
    CXY_COMPILER_STAGES(f)
#undef f
} CompilerStages;

// clang-format on

CompilerStages parseCompilerStages(cstring str);
const char *getCompilerStageName(CompilerStages stage);
const char *getCompilerStageDescription(CompilerStages stage);

AstNode *compilerParseFile(const CompilerDriver *driver, cstring filename);
AstNode *compilerParseString(const CompilerDriver *driver,
                             cstring code,
                             cstring filename);
AstNode *compilerDesugar(const CompilerDriver *driver, AstNode *node);
AstNode *compilerNameResolution(const CompilerDriver *driver, AstNode *node);
AstNode *compilerConstCheck(const CompilerDriver *driver, AstNode *node);
AstNode *compilerTypeCheck(const CompilerDriver *driver, AstNode *node);
AstNode *compilerComptimeEval(const CompilerDriver *driver, AstNode *node);
