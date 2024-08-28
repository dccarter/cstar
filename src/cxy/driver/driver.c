#include "driver.h"
#include "options.h"
#include "stages.h"

#include "core/log.h"
#include "core/mempool.h"
#include "core/utils.h"

#include "c.h"
#include "lang/frontend/ast.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/lexer.h"
#include "lang/frontend/parser.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"
#include "lang/middle/builtins.h"

#include "src/builtins.h"

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct CachedModule {
    cstring path;
    AstNode *program;
} CachedModule;

typedef struct ResolvedModulePath {
    cstring dir;
    cstring importPath;
    cstring codegenPath;
} ResolvedModulePath;

static bool compareCachedModules(const void *lhs, const void *rhs)
{
    return strcmp(((CachedModule *)lhs)->path, ((CachedModule *)rhs)->path) ==
           0;
}

static int compareModifiedTime(const struct stat *lhs, const struct stat *rhs)
{
    if (lhs->st_mtim.tv_sec < rhs->st_mtim.tv_sec) {
        return -1;
    }
    else if (lhs->st_mtim.tv_sec > rhs->st_mtim.tv_sec) {
        return 1;
    }
    else if (lhs->st_mtim.tv_nsec < rhs->st_mtim.tv_nsec) {
        return -1;
    }
    else if (lhs->st_mtim.tv_nsec > rhs->st_mtim.tv_nsec) {
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
    printStatus(driver->L, cWHT "Parsing %s..." cDEF, fileName);
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
    compilerStatsRecord(driver, ccsParse);

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
    printStatus(driver->L, cWHT "Parsing string @ %s" cDEF, fileName);
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

void makeDirectoryForPath(CompilerDriver *driver, cstring path)
{
    u64 len;
    char dir[PATH_MAX];
    cstring slash = strrchr(path, '/');
    if (slash == NULL)
        return;

    len = slash - path;
    if (len == 0)
        return;

    int n = snprintf(dir, PATH_MAX, "mkdir -p ");
    memcpy(&dir[n], path, len);
    dir[len + n] = 0;
    system(dir);
}

static inline bool hasDumpEnable(const Options *opts, const AstNode *node)
{
    if (opts->cmd == cmdDev) {
        return !hasFlag(node, BuiltinsModule) &&
               ((opts->dev.dumpMode != dmpNONE) || opts->dev.printIR);
    }
    return false;
}

static bool compileProgram(CompilerDriver *driver,
                           AstNode *program,
                           const char *fileName,
                           bool mainFile)
{
    const Options *options = &driver->options;
    bool status = true;

    AstNode *metadata = makeAstNode(
        driver->pool,
        builtinLoc(),
        &(AstNode){.tag = astMetadata,
                   .flags = program->flags & flgBuiltinsModule,
                   .metadata = {.filePath = fileName, .node = program}});

    CompilerStage stage = ccsParse + 1,
                  maxStage = (options->cmd == cmdDev && mainFile
                                  ? options->dev.lastStage + 1
                                  : ccsCOUNT);
    if (hasFlag(program, BuiltinsModule))
        maxStage = MAX(ccsTypeCheck + 1, maxStage);

    for (; stage < maxStage; stage++) {
        metadata = executeCompilerStage(driver, stage, metadata);
        if (metadata == NULL) {
            status = false;
            goto compileProgramDone;
        }
    }

    if (hasFlag(metadata, BuiltinsModule) || hasFlag(program, ImportedModule))
        return status;

    if (hasDumpEnable(options, metadata)) {
        stage = options->dev.printIR ? ccs_DumpIR : ccs_Dump;
        metadata = executeCompilerStage(driver, stage, metadata);
        if (metadata == NULL)
            status = false;
    }

compileProgramDone:
    stopCompilerStats(driver);
    bool dumpStats = !hasFlag(metadata, BuiltinsModule) &&
                     !(hasFlag(program, ImportedModule));
    if (dumpStats) {
        if (hasErrors(driver->L))
            printStatus(driver->L,
                        cBRED "\xE2\x9C\x92" cBWHT
                              " Compilation failure\n" cDEF);
        else
            printStatus(driver->L,
                        cBGRN "\xE2\x9C\x93" cBWHT
                              " Compilation success\n" cDEF);
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
    if (compileProgram(driver, program, fileName, false)) {
        insertAstNode(&driver->startup,
                      copyAstNode(driver->pool, program->program.decls));
        initializeBuiltins(driver->L, &program->loc, program->type);
        return true;
    }

    return false;
}

bool initCompilerDriver(CompilerDriver *compiler,
                        MemPool *pool,
                        StrPool *strings,
                        Log *log,
                        int argc,
                        char **argv)
{
    char tmp[PATH_MAX];
    compiler->pool = pool;
    compiler->strings = strings;
    compiler->types = newTypeTable(compiler->pool, compiler->strings);
    compiler->moduleCache = newHashTable(sizeof(CachedModule));
    compiler->nativeSources = newHashTable(sizeof(cstring));
    compiler->linkLibraries = newHashTable(sizeof(cstring));
    compiler->L = log;
    compiler->currentDir = makeString(compiler->strings, getcwd(tmp, PATH_MAX));
    compiler->currentDirLen = strlen(compiler->currentDir);

    internCommonStrings(compiler->strings);
    const Options *options = &compiler->options;
    compiler->backend = initCompilerBackend(compiler, argc, argv);
    csAssert0(compiler->backend);
    initCompilerPreprocessor(compiler);
    initCImporter(compiler);

    if (options->cmd == cmdBuild || !options->withoutBuiltins) {
        return compileBuiltin(compiler,
                              CXY_BUILTINS_SOURCE,
                              CXY_BUILTINS_SOURCE_SIZE,
                              "__builtins.cxy");
    }

    return true;
}

void deinitCompilerDriver(CompilerDriver *driver)
{
    deinitCompilerBackend(driver);
    deinitCompilerPreprocessor(driver);
    deinitCImporter(driver);
    freeHashTable(&driver->moduleCache);
    freeHashTable(&driver->linkLibraries);
    freeHashTable(&driver->nativeSources);
    freeTypeTable(driver->types);
    deinitCommandLineOptions(&driver->options);
}

static bool configureDriverSourceDir(CompilerDriver *driver, cstring *fileName)
{
    char buf[PATH_MAX];
    char *tmp = realpath(*fileName, buf);
    if (tmp == NULL) {
        logError(driver->L,
                 NULL,
                 "main source file {s} does not exist",
                 (FormatArg[]){{.s = buf}});
        return false;
    }
    driver->sourceDirLen = strrchr(tmp, '/') - tmp;
    driver->sourceDir =
        makeStringSized(driver->strings, tmp, driver->sourceDirLen);
    *fileName = makeString(driver->strings, tmp);
    return true;
}

static inline bool isImportModuleACHeader(cstring module)
{
    cstring ext = strrchr(module, '.');
    return ext != NULL && strcmp(ext + 1, "h") == 0;
}

static cstring getModuleLocation(CompilerDriver *driver, const AstNode *source)
{
    cstring importer = source->loc.fileName,
            modulePath = source->stringLiteral.value;
    csAssert0(modulePath && modulePath[0] != '\0');
    char path[PATH_MAX];
    u64 modulePathLen = strlen(modulePath);
    if (modulePath[0] == '.' && modulePath[1] == '/') {
        cstring importerFilename = strrchr(importer, '/');
        if (importerFilename == NULL)
            return modulePath;
        size_t importedLen = (importerFilename - importer) + 1;
        modulePathLen -= 2;
        memcpy(path, importer, importedLen);
        memcpy(&path[importedLen], modulePath + 2, modulePathLen);
        path[importedLen + modulePathLen] = '\0';
        char tmp[PATH_MAX];
        return makeString(driver->strings, realpath(path, tmp));
    }
    else if (driver->options.libDir != NULL) {
        char tmp[PATH_MAX];
        u64 libDirLen = strlen(driver->options.libDir);
        memcpy(path, driver->options.libDir, libDirLen);
        if (driver->options.libDir[libDirLen - 1] != '/')
            path[libDirLen++] = '/';
        memcpy(&path[libDirLen], modulePath, modulePathLen);
        path[libDirLen + modulePathLen] = '\0';
        return makeString(driver->strings, realpath(path, tmp));
    }
    else {
        char tmp[PATH_MAX];
        memcpy(path, driver->currentDir, driver->currentDirLen);
        if (driver->currentDir[driver->currentDirLen - 1] != '/')
            path[driver->currentDirLen] = '/';
        memcpy(&path[driver->currentDirLen + 1], modulePath, modulePathLen);
        path[driver->currentDirLen + 1 + modulePathLen] = '\0';
        if (realpath(path, tmp) == NULL) {
            logError(driver->L,
                     &source->loc,
                     "stdlib module '{s}' not found, perhaps import local "
                     "module with relative path './{s}'",
                     (FormatArg[]){{.s = modulePath}, {.s = modulePath}});
            return NULL;
        }
        return makeString(driver->strings, tmp);
    }
}

const Type *compileModule(CompilerDriver *driver,
                          const AstNode *source,
                          AstNode *entities,
                          AstNode *alias)
{
    AstNode *program = NULL;
    bool cached = true;
    cstring path = source->stringLiteral.value;
    if (!isImportModuleACHeader(source->stringLiteral.value)) {
        path = getModuleLocation(driver, source);
        if (path == NULL)
            return NULL;

        program = findCachedModule(driver, path);
        if (program == NULL) {
            cached = false;

            if (access(path, F_OK) != 0) {
                logError(driver->L,
                         &source->loc,
                         "module source file '{s}' does not exist",
                         (FormatArg[]){{.s = path}});
                return NULL;
            }

            program = parseFile(driver, path);
            if (program == NULL)
                return NULL;

            if (program->program.module == NULL) {
                logError(driver->L,
                         &source->loc,
                         "module source '{s}' is not declared as a module",
                         (FormatArg[]){{.s = path}});
                return NULL;
            }

            program->flags |= flgImportedModule;
            if (!compileProgram(driver, program, path, false))
                return NULL;
            AstNode *decls = program->program.decls;
            if (nodeIs(decls, ExternDecl) && hasFlag(decls, ModuleInit)) {
                // copy this declaration
                insertAstNode(&driver->startup,
                              copyAstNode(driver->pool, decls));
            }
        }
    }
    else {
        if (alias == NULL) {
            logError(
                driver->L,
                &source->loc,
                "importing a C header files requires an alias for the import",
                NULL);
            return NULL;
        }
        program = importCHeader(driver, source, alias->ident.value);
        if (program == NULL)
            return NULL;
    }

    AstNode *entity = entities;
    const Type *module = program->type;

    for (; entity; entity = entity->next) {
        const NamedTypeMember *member =
            findModuleMember(module, entity->importEntity.name);
        if (member == NULL) {
            logError(driver->L,
                     &entity->loc,
                     "module {s} does not export declaration with name '{s}'",
                     (FormatArg[]){{.s = source->stringLiteral.value},
                                   {.s = entity->importEntity.name}});
            continue;
        }

        if (!hasFlag(member->decl, Public)) {
            logError(
                driver->L,
                &entity->loc,
                "module {s} member'{s}' cannot be imported, it is not public",
                (FormatArg[]){{.s = source->stringLiteral.value},
                              {.s = entity->importEntity.name}});
            logNote(driver->L,
                    &member->decl->loc,
                    "`{s}` declared here",
                    (FormatArg[]){{.s = entity->importEntity.name}});
            continue;
        }

        entity->importEntity.target = (AstNode *)member->decl;
    }

    if (hasErrors(driver->L))
        return NULL;

    if (!cached)
        addCachedModule(driver, path, program);

    return program->type;
}

bool compileFile(const char *fileName, CompilerDriver *driver)
{
    if (!configureDriverSourceDir(driver, &fileName))
        return false;
    startCompilerStats(driver);
    AstNode *program = parseFile(driver, fileName);
    program->flags |= flgMain;

    return compileProgram(driver, program, fileName, true);
}

bool compileString(CompilerDriver *driver,
                   cstring source,
                   u64 size,
                   cstring filename)
{
    AstNode *program = parseString(driver, source, size, filename);
    return compileProgram(driver, program, filename, true);
}
