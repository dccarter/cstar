//
// Created by Carter Mbotho on 2023-07-05.
//

#pragma once

#include <lang/ast.h>

struct CompilerDriver;

// clang-format off
#define CXY_COMPILER_STAGES(f)                         \
    f(_Parse,           "Parse")                       \
    f(_Dump,            "Dump Ast")                    \
    f(_First,           "First Marker")                \
    f(Shake,            "Shake")                       \
    f(Bind,             "Bind")                        \
    f(ConstCheck,       "Const Check")                 \
    f(TypeCheck,        "Type Check")                  \
    f(Desugar,          "De-sugar")                    \
    f(Optimization,     "Optimization")                \
    f(MemoryMgmt,       "Memory Management")           \
    f(Finalize,         "Finalize")                    \
    f(Codegen,          "Code Gen")                    \
    f(Compile,          "Compile")                     \

typedef enum {
    ccsInvalid = 0,
#define f(NAME, ...) ccs##NAME,
    CXY_COMPILER_STAGES(f)
#undef f
    ccsCOUNT
} CompilerStage;

// clang-format on

u64 parseCompilerStages(Log *L, cstring str);
const char *getCompilerStageName(CompilerStage stage);
const char *getCompilerStageDescription(CompilerStage stage);

/**
 * Executes the given compiler id on the given AST node
 *
 * @param driver
 * @param stage
 * @param node
 *
 * @return The AST not after executing the given compiler id
 */
AstNode *executeCompilerStage(struct CompilerDriver *driver,
                              CompilerStage stage,
                              AstNode *node);
