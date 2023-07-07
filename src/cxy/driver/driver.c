#include "driver.h"
#include "cc.h"
#include "options.h"
#include "stages.h"

#include "core/log.h"
#include "core/mempool.h"
#include "core/utils.h"
#include "lang/ast.h"
#include "lang/lexer.h"
#include "lang/parser.h"
#include "lang/ttable.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct CachedModule {
    cstring path;
    AstNode *program;
} CachedModule;

static bool compareCachedModules(const void *lhs, const void *rhs)
{
    return strcmp(((CachedModule *)lhs)->path, ((CachedModule *)rhs)->path) ==
           0;
}

static int compareModifiedTime(const struct stat *lhs, const struct stat *rhs)
{
    if (lhs->st_mtimespec.tv_sec < rhs->st_mtimespec.tv_sec) {
        return -1;
    }
    else if (lhs->st_mtimespec.tv_sec > rhs->st_mtimespec.tv_sec) {
        return 1;
    }
    else if (lhs->st_mtimespec.tv_nsec < rhs->st_mtimespec.tv_nsec) {
        return -1;
    }
    else if (lhs->st_mtimespec.tv_nsec > rhs->st_mtimespec.tv_nsec) {
        return 1;
    }
    else {
        return 0;
    }
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
                                    sizeof(CachedModule),
                                    compareCachedModules);
    csAssert0(status);
}

attr(always_inline) static char *getCachedAstPath(Options *options,
                                                  const char *fileName)
{
    FormatState state = newFormatState("", true);
    format(&state,
           "{s}/cache/{s}",
           (FormatArg[]){{.s = options->buildDir}, {.s = fileName}});
    char *path = formatStateToString(&state);
    freeFormatState(&state);
    return path;
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

    compilerStatsSnapshot(driver);
    Lexer lexer = newLexer(fileName, fileData, file_size, driver->L);
    Parser parser = makeParser(&lexer, driver);
    AstNode *program = parseProgram(&parser);
    compilerStatsRecord(driver, ccs_Parse);

    freeLexer(&lexer);
    free(fileData);

    return program;
}

static AstNode *parseString(CompilerDriver *driver,
                            cstring code,
                            u64 codeSize,
                            const char *fileName)
{
    Lexer lexer = newLexer(fileName ?: "builtins", code, codeSize, driver->L);
    Parser parser = makeParser(&lexer, driver);
    AstNode *program = parseProgram(&parser);

    freeLexer(&lexer);

    return program;
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

    format(&state,
           "{s}/{s}/{s}{s}",
           (FormatArg[]){{.s = options->buildDir ?: "./"},
                         {.s = dir},
                         {.s = fileName},
                         {.s = ext}});

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
    //    generateCode(
    //        &state, driver->typeTable, &driver->strPool, program, isImport);
    writeFormatState(&state, output);
    freeFormatState(&state);
    fclose(output);

    if (!isImport && options->cmd == cmdDev) {
        compileCSourceFile(driver, sourceFile);
    }

    free(sourceFile);

    return true;
}

static inline bool hasDumpEnable(const Options *opts)
{
    return opts->cmd == cmdDev && opts->dev.printAst;
}

static bool compileProgram(CompilerDriver *driver,
                           AstNode *program,
                           const char *fileName)
{
    MemPoolStats stats;
    const Options *options = &driver->options;
    bool status = true;

    CompilerStage stage = ccs_First + 1,
                  maxStage =
                      (options->cmd == cmdDev ? options->dev.lastStage.num + 1
                                              : ccsCOUNT);

    for (; stage < maxStage; stage++) {
        program = executeCompilerStage(driver, stage, program);
        if (program == NULL) {
            status = false;
            goto compileProgramDone;
        }
    }

    if (hasDumpEnable(options)) {
        program = executeCompilerStage(driver, ccs_Dump, program);
        if (program == NULL)
            status = false;
    }

compileProgramDone:
    compilerStatsPrint(driver);
    return true;
}

static bool compileBuiltin(CompilerDriver *driver,
                           cstring code,
                           u64 size,
                           const char *fileName)
{
    const Options *options = &driver->options;
    AstNode *program = parseString(driver, code, size, fileName);
    if (program == NULL)
        return false;

    return true;
}

bool initCompilerDriver(CompilerDriver *compiler, Log *log)
{
    compiler->pool = newMemPool();
    compiler->strPool = newStrPool(&compiler->pool);
    compiler->typeTable = newTypeTable(&compiler->pool, &compiler->strPool);
    compiler->moduleCache = newHashTable(sizeof(CachedModule));
    compiler->L = log;
    return true;
}

AstNode *compileModule(CompilerDriver *driver,
                       const AstNode *source,
                       const AstNode *entities)
{
    unreachable("TODO");
}

bool compileFile(const char *fileName, CompilerDriver *driver)
{
    if (driver->options.cmd == cmdDev && !generateBuiltinSources(driver))
        return false;

    AstNode *program = parseFile(driver, fileName);

    return compileProgram(driver, program, fileName);
}

bool compileString(CompilerDriver *driver,
                   cstring source,
                   u64 size,
                   cstring filename)
{
    AstNode *program = parseString(driver, source, size, filename);
    return compileProgram(driver, program, filename);
}
