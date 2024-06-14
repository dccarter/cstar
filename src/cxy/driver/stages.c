//
// Created by Carter Mbotho on 2023-07-05.
//

#include "stages.h"

#include "lang/frontend/flag.h"
#include "lang/operations.h"

#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

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
    insertInHashTable(                                                         \
        stages,                                                                \
        &(Stage) { #name, strlen(#name), ccs##name },                          \
        hashStr(hashInit(), #name),                                            \
        sizeof(Stage),                                                         \
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

    if (driver->options.dev.dumpMode == dmpJSON)
        return dumpAstJson(driver, node->metadata.node, file);
    else if (driver->options.dev.dumpMode == dmpYAML)
        return dumpAstToYaml(driver, node, file);
    else
        return dumpCxySource(driver, node, file);
}

static AstNode *executeDumpIR(CompilerDriver *driver, AstNode *node)
{
    csAssert0(nodeIs(node, Metadata));
    node->metadata.stages |= BIT(ccs_DumpIR);

    return backendDumpIR(driver, node);
}

static AstNode *executePreprocessAst(CompilerDriver *driver, AstNode *node)
{
    csAssert0(nodeIs(node, Metadata));
    node->metadata.node = preprocessAst(driver, node->metadata.node);

    if (hasErrors(driver->L))
        return NULL;
    node->metadata.stages |= BIT(ccsPreprocess);
    return node;
}

static AstNode *executeShakeAst(CompilerDriver *driver, AstNode *node)
{
    csAssert0(nodeIs(node, Metadata));
    if (!(node->metadata.stages & BIT(ccsPreprocess))) {
        logError(
            driver->L, builtinLoc(), "cannot shake an unprocessed ast", NULL);
        return NULL;
    }

    node->metadata.node = shakeAstNode(driver, node->metadata.node);

    if (hasErrors(driver->L))
        return NULL;
    node->metadata.stages |= BIT(ccsShake);
    return node;
}

static AstNode *executeBindAst(CompilerDriver *driver, AstNode *node)
{
    csAssert0(nodeIs(node, Metadata));
    if (!(node->metadata.stages & BIT(ccsShake))) {
        logError(driver->L, builtinLoc(), "cannot bind an unshaken ast", NULL);
        return NULL;
    }

    node->metadata.node = bindAst(driver, node->metadata.node);

    if (hasErrors(driver->L))
        return NULL;

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

static AstNode *executeSimplify(CompilerDriver *driver, AstNode *node)
{
    csAssert0(nodeIs(node, Metadata));
    if (!(node->metadata.stages & BIT(ccsTypeCheck))) {
        logError(
            driver->L, builtinLoc(), "cannot simplify an untyped ast", NULL);
        return NULL;
    }

    node->metadata.node = simplifyAst(driver, node->metadata.node);

    if (hasErrors(driver->L))
        return NULL;

    node->metadata.stages |= BIT(ccsSimplify);
    return node;
}

static AstNode *executeMemoryManagement(CompilerDriver *driver, AstNode *node)
{
    csAssert0(nodeIs(node, Metadata));
    if (!driver->options.withMemoryManager)
        return node;
    if (!(node->metadata.stages & BIT(ccsSimplify))) {
        logError(driver->L,
                 builtinLoc(),
                 "AST must be simplified before memory management",
                 NULL);
        return NULL;
    }

    node->metadata.node = memoryManageAst(driver, node->metadata.node);

    if (hasErrors(driver->L))
        return NULL;

    node->metadata.stages |= BIT(ccsMemoryMgmt);
    return node;
}

static AstNode *executeGenerateCode(CompilerDriver *driver, AstNode *node)
{
    csAssert0(nodeIs(node, Metadata));
    if (!(node->metadata.stages & BIT(ccsSimplify))) {
        logError(driver->L,
                 builtinLoc(),
                 "cannot generate code before the AST has been memory managed",
                 NULL);
        return NULL;
    }

    FormatState state = newFormatState("  ", true);
    node->metadata.state = &state;
    generateCode(driver, node);
    freeFormatState(&state);
    node->metadata.state = NULL;

    if (hasErrors(driver->L))
        return NULL;

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

    if (!compilerBackendMakeExecutable(driver))
        return NULL;

    if (hasErrors(driver->L))
        return NULL;

    node->metadata.stages |= BIT(ccsCompile);
    return node;
}

static AstNode *executeCollect(CompilerDriver *driver, AstNode *node)
{
    csAssert0(nodeIs(node, Metadata));
    if (!(node->metadata.stages & BIT(ccsCodegen))) {
        logError(driver->L,
                 builtinLoc(),
                 "cannot collect an AST before code generation",
                 NULL);
        return NULL;
    }

    node->metadata.node = collectAst(driver, node->metadata.node);

    if (hasErrors(driver->L))
        return NULL;

    node->metadata.stages |= BIT(ccsCollect);
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
    [ccs_DumpIR] = executeDumpIR,
    [ccsPreprocess] = executePreprocessAst,
    [ccsShake] = executeShakeAst,
    [ccsBind] = executeBindAst,
    [ccsTypeCheck] = executeTypeCheckAst,
    [ccsSimplify] = executeSimplify,
    [ccsMemoryMgmt] = executeMemoryManagement,
    [ccsCodegen] = executeGenerateCode,
    // TODO causing issues
    // [ccsCollect] = executeCollect,
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
                         "unsupported compiler stage id '{s}'",
                         (FormatArg[]){{.s = stageName}});
        return node;
    }

    cstring name =
        nodeIs(node, Metadata) ? node->metadata.filePath : node->loc.fileName;
    printStatus(
        driver->L, cBWHT "* %s %s..." cDEF, stageName, name ?: "<unknown>");
    compilerStatsSnapshot(driver);
    node = executor(driver, node);
    compilerStatsRecord(driver, stage);

    if (hasErrors(driver->L))
        return NULL;

    return node;
}
