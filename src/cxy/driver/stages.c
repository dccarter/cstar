//
// Created by Carter Mbotho on 2023-07-05.
//

#include "stages.h"
#include "cc.h"

#include "lang/flag.h"
#include "lang/operations.h"

#include <ctype.h>
#include <errno.h>
#include <string.h>

typedef struct {
    const char *name;
    size_t len;
    CompilerStage stage;
} Stage;

static bool compareStages(const void *left, const void *right)
{
    return ((Stage *)left)->len == ((Stage *)right)->len &&
           !memcmp(((Stage *)left)->name,
                   ((Stage *)right)->name,
                   ((Stage *)left)->len);
}

static void registerStages(HashTable *stages)
{
#define f(name, ...)                                                           \
    insertInHashTable(stages,                                                  \
                      &(Stage){#name, strlen(#name), ccs##name},               \
                      hashStr(hashInit(), #name),                              \
                      sizeof(Stage),                                           \
                      compareStages);
    CXY_COMPILER_STAGES(f)
#undef f
}

static CompilerStage parseNextCompilerStage(Log *L, char *start, char *end)
{
    static bool initialized = false;
    static HashTable stages;
    if (!initialized) {
        initialized = true;
        stages = newHashTable(sizeof(Stage));
        registerStages(&stages);
    }
    char *p = start;
    while (isspace(*p))
        p++;
    if (*p == '0') {
        logError(L,
                 NULL,
                 "parsing id('s) failed, expecting a id name (got '{s}')",
                 (FormatArg[]){{.s = start}});
        return ccsInvalid;
    }

    u64 len;
    if (end) {
        while (isspace(*end))
            end--;
        end[1] = '\0';
        len = end - p;
    }
    else
        len = strlen(p);

    Stage *stage = findInHashTable(&stages,
                                   &(Stage){.name = p, .len = len},
                                   hashRawBytes(hashInit(), p, len),
                                   sizeof(Stage),
                                   compareStages);

    return stage ? stage->stage : ccsInvalid;
}

typedef AstNode *(*CompilerStageExecutor)(CompilerDriver *, AstNode *);

static AstNode *executeDumpAst(CompilerDriver *driver, AstNode *node)
{
    csAssert0(nodeIs(node, Metadata));
    node->metadata.stages |= BIT(ccs_Dump);

    FILE *file = stdout;
    if (driver->options.output) {
        file = fopen(driver->options.output, "w+");
        if (file == NULL) {
            logError(driver->L,
                     NULL,
                     "opening output file '{s}' failed: {s}",
                     (FormatArg[]){{.s = driver->options.output},
                                   {.s = strerror(errno)}});
            return NULL;
        }
    }

    if (driver->options.dev.dumpJson)
        return dumpAstJson(driver, node->metadata.node, file);
    else
        return dumpAstToYaml(driver, node, file);
}

static AstNode *executeShakeAst(CompilerDriver *driver, AstNode *node)
{
    csAssert0(nodeIs(node, Metadata));
    node->metadata.node = shakeAstNode(driver, node->metadata.node);

    if (hasErrors(driver->L))
        return NULL;
    node->metadata.stages |= BIT(ccsShake);
    return node;
}

static AstNode *executeBindAst(CompilerDriver *driver, AstNode *node)
{
    csAssert0(nodeIs(node, Metadata));
    node->metadata.node = bindAst(driver, node->metadata.node);

    if (hasErrors(driver->L))
        return NULL;
    ;

    node->metadata.stages |= BIT(ccsBind);
    return node;
}

static AstNode *executeTypeCheckAst(CompilerDriver *driver, AstNode *node)
{
    csAssert0(nodeIs(node, Metadata));
    if (!(node->metadata.stages & BIT(ccsBind))) {
        logError(
            driver->L, builtinLoc(), "cannot type check an unbound ast", NULL);
        return NULL;
    }

    node->metadata.node = checkAst(driver, node->metadata.node);

    if (hasErrors(driver->L))
        return NULL;

    node->metadata.stages |= BIT(ccsTypeCheck);
    return node;
}

static AstNode *executeFinalizeAst(CompilerDriver *driver, AstNode *node)
{
    csAssert0(nodeIs(node, Metadata));
    if (!(node->metadata.stages & BIT(ccsTypeCheck))) {
        logError(
            driver->L, builtinLoc(), "cannot finalize an untyped ast", NULL);
        return NULL;
    }

    node->metadata.node = finalizeAst(driver, node->metadata.node);

    if (hasErrors(driver->L))
        return NULL;

    node->metadata.stages |= BIT(ccsFinalize);
    return node;
}

static AstNode *executeGenerateCode(CompilerDriver *driver, AstNode *node)
{
    csAssert0(nodeIs(node, Metadata));
    if (!(node->metadata.stages & BIT(ccsFinalize))) {
        logError(driver->L,
                 builtinLoc(),
                 "cannot generate code for an un-finalized ast",
                 NULL);
        return NULL;
    }

    FormatState state = newFormatState("  ", true);
    node->metadata.state = &state;
    generateCode(driver, node);

    if (hasErrors(driver->L))
        return NULL;

    bool status = createSourceFile(
        driver, &state, &node->metadata.filePath, node->metadata.node->flags);
    freeFormatState(&state);

    if (!status) {
        return NULL;
    }

    node->metadata.stages |= BIT(ccsCodegen);
    node->metadata.state = NULL;

    return node;
}

static AstNode *executeTargetCompile(CompilerDriver *driver, AstNode *node)
{
    csAssert0(nodeIs(node, Metadata));
    if (!(node->metadata.stages & BIT(ccsCodegen))) {
        logError(driver->L,
                 builtinLoc(),
                 "cannot compile generated for an un-generated AST",
                 NULL);
        return NULL;
    }

    if (!hasFlag(node->metadata.node, Main))
        return node;

    compileCSourceFile(driver, node->metadata.filePath);
    free((void *)node->metadata.filePath);
    node->metadata.stages |= BIT(ccsCompile);
    return node;
}

const char *getCompilerStageName(CompilerStage stage)
{
    switch (stage) {
#define f(NAME, ...)                                                           \
    case ccs##NAME:                                                            \
        return #NAME;
        CXY_COMPILER_STAGES(f)
#undef f
    default:
        return "<nothing>";
    }
}

const char *getCompilerStageDescription(CompilerStage stage)
{
    switch (stage) {
#define f(NAME, DESC)                                                          \
    case ccs##NAME:                                                            \
        return DESC;
        CXY_COMPILER_STAGES(f)
#undef f
    default:
        return "<nothing>";
    }
}

u64 parseCompilerStages(Log *L, cstring str)
{
    CompilerStage stages = ccsInvalid;
    char *copy = strdup(str);
    char *start = copy, *end = strchr(str, '|');

    while (start) {
        char *last = end;
        if (last) {
            *last = '\0';
            last--;
            end++;
        }
        if (start[0] == '_') {
            logError(L,
                     NULL,
                     "parsing compiler id failed, '{s}' is an internal id",
                     (FormatArg[]){{.s = start}});
            return ccsInvalid;
        }

        CompilerStage stage = parseNextCompilerStage(L, start, last);
        if (stage == ccsInvalid)
            return ccsInvalid;

        stages |= (1 << stage);
        while (end && *end == '|')
            end++;

        start = end;
        end = last ? strchr(last, '|') : NULL;
    }

    free(copy);

    return stages;
}

static CompilerStageExecutor compilerStageExecutors[ccsCOUNT] = {
    [ccsInvalid] = NULL,
    [ccs_Dump] = executeDumpAst,
    [ccsShake] = executeShakeAst,
    [ccsBind] = executeBindAst,
    [ccsTypeCheck] = executeTypeCheckAst,
    [ccsFinalize] = executeFinalizeAst,
    [ccsCodegen] = executeGenerateCode,
    [ccsCompile] = executeTargetCompile};

AstNode *executeCompilerStage(CompilerDriver *driver,
                              CompilerStage stage,
                              AstNode *node)
{
    cstring stageName = getCompilerStageName(stage);
    CompilerStageExecutor executor = compilerStageExecutors[stage];
    if (executor == NULL) {
        logWarningWithId(driver->L,
                         wrnMissingStage,
                         NULL,
                         "unsupported compiler id '{s}'",
                         (FormatArg[]){{.s = stageName}});
        return node;
    }

    if (driver->options.progress) {
        logNote(driver->L,
                NULL,
                "executing '{s}' id",
                (FormatArg[]){{.s = stageName}});
    }

    compilerStatsSnapshot(driver);
    node = executor(driver, node);
    compilerStatsRecord(driver, stage);

    if (hasErrors(driver->L))
        return NULL;

    return node;
}
