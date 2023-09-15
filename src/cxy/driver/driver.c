#include "driver.h"
#include "builtins.h"
#include "cc.h"
#include "options.h"
#include "stages.h"

#include "core/log.h"
#include "core/mempool.h"
#include "core/utils.h"

#include "lang/ast.h"
#include "lang/builtins.h"
#include "lang/flag.h"
#include "lang/lexer.h"
#include "lang/parser.h"
#include "lang/strings.h"
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

cstring getFilenameWithoutDirs(cstring fileName)
{
    if (fileName[0] == '/') {
        const char *slash = strrchr(fileName, '/');
        if (slash) {
            fileName = slash + 1;
        }
    }

    return fileName;
}

char *getGeneratedPath(const Options *options,
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

static inline bool hasDumpEnable(const Options *opts)
{
    return opts->cmd == cmdDev && opts->dev.printAst;
}

static bool compileProgram(CompilerDriver *driver,
                           AstNode *program,
                           const char *fileName)
{
    const Options *options = &driver->options;
    bool status = true;

    AstNode *metadata = makeAstNode(
        &driver->pool,
        builtinLoc(),
        &(AstNode){.tag = astMetadata,
                   .flags = program->flags & flgBuiltinsModule,
                   .metadata = {.filePath = fileName, .node = program}});

    CompilerStage stage = ccs_First + 1,
                  maxStage =
                      (options->cmd == cmdDev ? options->dev.lastStage.num + 1
                                              : ccsCOUNT);

    for (; stage < maxStage; stage++) {
        metadata = executeCompilerStage(driver, stage, metadata);
        if (metadata == NULL) {
            status = false;
            goto compileProgramDone;
        }
    }

    if (hasDumpEnable(options)) {
        metadata = executeCompilerStage(driver, ccs_Dump, metadata);
        if (metadata == NULL)
            status = false;
    }

compileProgramDone:
    stopCompilerStats(driver);
    bool dumpStats = !options->dev.cleanAst &&
                     !hasFlag(metadata, BuiltinsModule) &&
                     !(hasFlag(program, ImportedModule));
    if (dumpStats) {
        compilerStatsPrint(driver);
    }
    return status;
}

static bool compileBuiltin(CompilerDriver *driver,
                           cstring code,
                           u64 size,
                           const char *fileName)
{
    AstNode *program = parseString(driver, code, size, fileName);
    if (program == NULL)
        return false;

    program->flags |= flgBuiltinsModule;
    if (compileProgram(driver, program, fileName)) {
        initializeBuiltins(driver->L, &program->loc, program->type);
        return true;
    }

    return false;
}

bool initCompilerDriver(CompilerDriver *compiler, Log *log)
{
    compiler->pool = newMemPool();
    compiler->strPool = newStrPool(&compiler->pool);
    compiler->typeTable = newTypeTable(&compiler->pool, &compiler->strPool);
    compiler->moduleCache = newHashTable(sizeof(CachedModule));
    compiler->L = log;
    internCommonStrings(&compiler->strPool);

    if (compiler->options.cmd == cmdBuild) {
        if (!generateAllBuiltinSources(compiler))
            return false;

        return compileBuiltin(compiler,
                              CXY_BUILTINS_SOURCE,
                              CXY_BUILTINS_SOURCE_SIZE,
                              "__builtins.cxy");
    }

    return true;
}

const Type *compileModule(CompilerDriver *driver,
                          const AstNode *source,
                          AstNode *entities)
{
    cstring name = source->stringLiteral.value;
    AstNode *program = findCachedModule(driver, name);
    bool cached = true;
    if (program == NULL) {
        cached = false;

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
            return NULL;

        program->flags |= flgImportedModule;
        if (!compileProgram(driver, program, name))
            return NULL;
    }

    AstNode *entity = entities;
    const Type *module = program->type;

    for (; entity; entity = entity->next) {
        const ModuleMember *member =
            findModuleMember(module, entity->importEntity.name);
        if (member) {
            entity->importEntity.target = (AstNode *)member->decl;
        }
        else {
            logError(
                driver->L,
                &entity->loc,
                "module {s} does not export declaration with name '{s}'",
                (FormatArg[]){{.s = name}, {.s = entity->importEntity.name}});
        }
    }

    if (hasErrors(driver->L))
        return NULL;

    if (!cached)
        addCachedModule(driver, name, program);

    return program->type;
}

bool compileFile(const char *fileName, CompilerDriver *driver)
{
    startCompilerStats(driver);
    AstNode *program = parseFile(driver, fileName);
    program->flags |= flgMain;
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
