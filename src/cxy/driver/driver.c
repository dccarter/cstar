#include "driver.h"
#include "cc.h"

#include "core/log.h"
#include "core/mempool.h"
#include "core/utils.h"
#include "driver/options.h"
#include "lang/ast.h"
#include "lang/codegen.h"
#include "lang/lexer.h"
#include "lang/parser.h"
#include "lang/semantics.h"
#include "lang/ttable.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#define BYTES_TO_GB(B) (((double)(B)) / 1000000000)
#define BYTES_TO_MB(B) (((double)(B)) / 1000000)
#define BYTES_TO_KB(B) (((double)(B)) / 1000)

typedef struct CachedModule {
    cstring path;
    AstNode *program;
} CachedModule;

static bool compareCachedModules(const void *lhs, const void *rhs)
{
    return strcmp(((CachedModule *)lhs)->path, ((CachedModule *)rhs)->path) ==
           0;
}

static AstNode *findCachedModule(CompilerDriver *driver, cstring path)
{
    u32 hash = hashStr(hashInit(), path);
    CachedModule module = (CachedModule){.path = path};
    CachedModule *found = findInHashTable(&driver->moduleCache, //
                                          &module,
                                          hash,
                                          sizeof(CachedModule),
                                          compareCachedModules);
    if (found)
        return found->program;
    return NULL;
}

static void addCachedModule(CompilerDriver *driver,
                            cstring path,
                            AstNode *program)
{
    u32 hash = hashStr(hashInit(), path);
    CachedModule module = (CachedModule){.path = path, .program = program};
    bool status = insertInHashTable(&driver->moduleCache,
                                    &module,
                                    hash,
                                    sizeof(CachedModule *),
                                    compareCachedModules);
    csAssert0(status);
}

static AstNode *parseFile(CompilerDriver *driver, const char *fileName)
{
    size_t file_size = 0;
    char *fileData = readFile(fileName, &file_size);
    if (!fileData) {
        logError(driver->L,
                 NULL,
                 "cannot open file '{s}'",
                 (FormatArg[]){{.s = fileName}});
        return NULL;
    }

    Lexer lexer = newLexer(fileName, fileData, file_size, driver->L);
    Parser parser = makeParser(&lexer, driver);
    AstNode *program = parseProgram(&parser);

    freeLexer(&lexer);
    free(fileData);

    return program;
}

static inline bool hasErrors(CompilerDriver *driver)
{
    return driver->L->errorCount > 0;
}

static cstring getFilenameWithoutDirs(cstring fileName)
{
    if (fileName[0] == '/') {
        const char *slash = strrchr(fileName, '/');
        if (slash) {
            fileName = slash + 1;
        }
    }

    return fileName;
}

static char *getGeneratedPath(const Options *options,
                              cstring dir,
                              cstring filePath,
                              cstring ext)
{
    FormatState state = newFormatState("    ", true);
    cstring fileName = getFilenameWithoutDirs(filePath);

    format(
        &state,
        "{s}/{s}/{s}{s}",
        (FormatArg[]){
            {.s = options->buildDir}, {.s = dir}, {.s = fileName}, {.s = ext}});

    char *path = formatStateToString(&state);
    freeFormatState(&state);

    return path;
}

void makeDirectoryForPath(CompilerDriver *driver, cstring path)
{
    u64 len;
    char dir[512];
    cstring slash = strrchr(path, '/');
    if (slash == NULL)
        return;

    len = slash - path;
    if (len == 0)
        return;

    int n = sprintf(dir, "mkdir -p ");
    memcpy(&dir[n], path, len);
    dir[len + n] = 0;
    system(dir);
}

static bool generateSourceFiles(CompilerDriver *driver,
                                AstNode *program,
                                cstring filePath,
                                bool isImport)
{
    const Options *options = &driver->options;
    char *sourceFile = getGeneratedPath(
        &driver->options, isImport ? "c/imports" : "c/src", filePath, ".c");

    makeDirectoryForPath(driver, sourceFile);

    FILE *output = fopen(sourceFile, "w");
    if (output == NULL) {
        logError(driver->L,
                 NULL,
                 "creating output file '{s}' failed, {s}",
                 (FormatArg[]){{.s = options->output}, {.s = strerror(errno)}});
        free(sourceFile);
        return false;
    }

    FormatState state = newFormatState("  ", true);
    generateCode(
        &state, driver->typeTable, &driver->strPool, program, isImport);
    writeFormatState(&state, output);
    freeFormatState(&state);
    fclose(output);

    if (!isImport && options->cmd == cmdBuild) {
        compileCSourceFile(driver, sourceFile);
    }

    free(sourceFile);

    return true;
}

static void dumpGeneratedAst(CompilerDriver *driver, const AstNode *program)
{
    FormatState state = newFormatState(
        "    ", driver->L->state->ignoreStyle || !isColorSupported(stdout));
    printAst(&state, program);
    writeFormatState(&state, stdout);
    freeFormatState(&state);
    printf("\n");
}

void initCompilerDriver(CompilerDriver *compiler, Log *log)
{
    compiler->options = default_options;
    compiler->memPool = newMemPool();
    compiler->strPool = newStrPool(&compiler->memPool);
    compiler->typeTable = newTypeTable(&compiler->memPool, &compiler->strPool);
    compiler->L = log;
}

AstNode *compileModule(CompilerDriver *driver,
                       const AstNode *source,
                       const AstNode *entities)
{
    const Options *options = &driver->options;
    AstNode *program = NULL;
    cstring name = source->stringLiteral.value;
    bool cached = false;
    program = findCachedModule(driver, name);

    if (program == NULL) {
        cached = true;

        if (access(name, F_OK) != 0) {
            logError(driver->L,
                     &source->loc,
                     "module source file '{s}' does not exist",
                     (FormatArg[]){{.s = name}});
            return NULL;
        }

        program = parseFile(driver, name);
        if (program->program.module == NULL) {
            logError(driver->L,
                     &source->loc,
                     "module source '{s}' is not declared as a module",
                     (FormatArg[]){{.s = name}});
            return NULL;
        }

        if (program == NULL)
            return false;

        if (options->cmd == cmdBuild ||
            (!options->noTypeCheck && !hasErrors(driver))) {
            semanticsCheck(program,
                           driver->L,
                           &driver->memPool,
                           &driver->strPool,
                           driver->typeTable);
        }

        if (hasErrors(driver))
            return NULL;
    }

    const AstNode *entity = entities;
    AstNode *module = program->program.module;

    for (; entity; entity = entity->next) {
        if (!findSymbolOnly(module->moduleDecl.env,
                            entity->importEntity.name)) {
            logError(
                driver->L,
                &entity->loc,
                "module {s} does not export declaration with name '{s}'",
                (FormatArg[]){{.s = name}, {.s = entity->importEntity.name}});
        }
    }

    if (hasErrors(driver))
        return NULL;

    if (!cached) {
        generateSourceFiles(driver, program, name, true);
        addCachedModule(driver, name, program);
    }

    return module;
}

bool compileSource(const char *fileName, CompilerDriver *driver)
{
    const Options *options = &driver->options;
    AstNode *program = parseFile(driver, fileName);

    if (program == NULL)
        return false;

    if (options->cmd == cmdBuild ||
        (!options->noTypeCheck && !hasErrors(driver))) {
        semanticsCheck(program,
                       driver->L,
                       &driver->memPool,
                       &driver->strPool,
                       driver->typeTable);
    }

    if (options->printAst && !hasErrors(driver)) {
        dumpGeneratedAst(driver, program);
        return true;
    }

    if (!hasErrors(driver)) {
        generateSourceFiles(driver, program, fileName, false);
    }

    MemPoolStats stats;
    getMemPoolStats(&driver->memPool, &stats);
    printf("\tMemory usage: blocks: %zu, allocated: %f kb, used: %f kb\n",
           stats.numberOfBlocks,
           BYTES_TO_KB(stats.totalAllocated),
           BYTES_TO_KB(stats.totalUsed));

    return true;
}
