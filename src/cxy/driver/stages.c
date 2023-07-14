//
// Created by Carter Mbotho on 2023-07-05.
//

#include "stages.h"

#include "lang/codegen.h"
#include "lang/operations.h"
#include "lang/semantics.h"

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
    if (!nodeIs(node, Metadata)) {
        logError(
            driver->L, NULL, "dump only supported on metadata nodes", NULL);
        return NULL;
    }
    node->metadata.stages |= (1 << ccs_Dump);

    node = dumpAst(driver, node);

    if (driver->options.output) {
        FILE *fp = fopen(driver->options.output, "w+");
        if (fp == NULL) {
            logError(driver->L,
                     NULL,
                     "opening output file '{s}' failed: {s}",
                     (FormatArg[]){{.s = driver->options.output},
                                   {.s = strerror(errno)}});
            goto dumpExit;
        }
        fputs(node->metadata.node->stringLiteral.value, fp);
        putc('\n', stdout);
    }
    else {
        fputs(node->metadata.node->stringLiteral.value, stdout);
        putc('\n', stdout);
    }

dumpExit:
    free((void *)node->metadata.node->stringLiteral.value);
    node->tag = astNop;
    return node;
}

static AstNode *executeShakeAst(CompilerDriver *driver, AstNode *node)
{
    if (!nodeIs(node, Metadata) || node->metadata.stages & ccsShake) {
        logError(driver->L, NULL, "cannot shake an already shaken node", NULL);
        return node;
    }

    SemanticsContext context = {.L = driver->L,
                                .typeTable = driver->typeTable,
                                .pool = &driver->pool,
                                .strPool = &driver->strPool,
                                .program = NULL,
                                .env = NULL,
                                .exports = NULL,
                                .isBuiltins = driver->builtins == NULL};
    semanticsCheck(&context, node);

    if (hasErrors(driver->L))
        return NULL;

    node->metadata.stages |= (1 << ccsShake);
    return node;
}

static AstNode *executeCodegen(CompilerDriver *driver, AstNode *node)
{
    Options *options = &driver->options;
    if (!nodeIs(node, Metadata)) {
        logError(driver->L, NULL, "missing metadata", NULL);
        return node;
    }

    FormatState state = newFormatState("  ", true);
    CodegenContext context = {.state = &state,
                              .types = driver->typeTable,
                              .strPool = &driver->strPool,
                              .importedFile =
                                  node->metadata.node->program.module != NULL};

    char *sourceFile =
        getGeneratedPath(&driver->options,
                         context.importedFile ? "c/imports" : "c/src",
                         node->metadata.filePath,
                         ".c");
    makeDirectoryForPath(driver, sourceFile);

    FILE *output = fopen(sourceFile, "w");
    if (output == NULL) {
        logError(driver->L,
                 NULL,
                 "creating output file '{s}' failed, {s}",
                 (FormatArg[]){{.s = options->output}, {.s = strerror(errno)}});
        freeFormatState(&state);
        free(sourceFile);
        return NULL;
    }

    generateCode(&context, node);

    if (hasErrors(driver->L)) {
        freeFormatState(&state);
        free(sourceFile);
        return NULL;
    }

    writeFormatState(&state, output);
    freeFormatState(&state);
    fclose(output);

    node->metadata.filePath = sourceFile;
    node->metadata.stages |= (1 << ccsCodegen);
    return node;
}

static CompilerStageExecutor compilerStageExecutors[ccsCOUNT] = {
    [ccsInvalid] = NULL,
    [ccs_Dump] = executeDumpAst,
    [ccsShake] = executeShakeAst,
    [ccsCodegen] = executeCodegen};

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
