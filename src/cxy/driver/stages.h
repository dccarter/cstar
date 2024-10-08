//
// Created by Carter Mbotho on 2023-07-05.
//

#pragma once

#include "lang/frontend/ast.h"

#ifdef __cplusplus
extern "C" {
#endif

struct CompilerDriver;

// clang-format off
#define CXY_PRIVATE_COMPILER_STAGES(f)                 \
    f(_Dump,            "Dump Ast")                    \
    f(_DumpIR,          "Dump IR")                     \
    f(_First,           "First Marker")                \

#define CXY_PUBLIC_COMPILER_STAGES(f)                  \
    f(Parse,            "Parse")                       \
    f(Preprocess,       "Preprocess")                  \
    f(Shake,            "Shake")                       \
    f(Bind,             "Bind")                        \
    f(ConstCheck,       "Const Check")                 \
    f(TypeCheck,        "Type Check")                  \
    f(Simplify,         "Simplify")                    \
    f(MemoryMgmt,       "Memory Mgmt")                 \
    f(Lower,            "Lower")                       \
    f(Finalize,         "Finalize")                    \
    f(Codegen,          "Code Gen")                    \
    f(Collect,          "Collect")                     \
    f(Compile,          "Compile")                     \

#define CXY_COMPILER_STAGES(f)                         \
    CXY_PRIVATE_COMPILER_STAGES(f)                     \
    CXY_PUBLIC_COMPILER_STAGES(f)


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

#ifdef __cplusplus
}
#endif
