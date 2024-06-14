//

#include "driver/options.h"
#include "driver/stages.h"

#include "core/args.h"
#include "core/log.h"
#include "core/strpool.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    cstring name;
    u64 value;
} EnumOptionMap;

static bool cmdParseDumpAstModes(CmdParser *P,
                                 CmdFlagValue *dst,
                                 const char *str,
                                 const char *name)
{
#define DUMP_OPTIONS_COUNT (sizeof(dumpModes) / sizeof(EnumOptionMap))
    static CmdEnumValueDesc dumpModes[] = {
#define ff(NN) {#NN, dmp##NN},
        DUMP_OPTIONS(ff)
#undef ff
    };
    return cmdParseEnumValue(P, dst, str, name, dumpModes, DUMP_OPTIONS_COUNT);
#undef DUMP_OPTIONS_COUNT
}

static bool cmdParseDumpStatsModes(CmdParser *P,
                                   CmdFlagValue *dst,
                                   const char *str,
                                   const char *name)
{
#define DUMP_STATS_MODES_COUNT (sizeof(dumpModes) / sizeof(EnumOptionMap))
    static CmdEnumValueDesc dumpModes[] = {
#define ff(NN) {#NN, dsm##NN},
        DRIVER_STATS_MODE(ff)
#undef ff
    };
    return cmdParseEnumValue(
        P, dst, str, name, dumpModes, DUMP_STATS_MODES_COUNT);
#undef DUMP_STATS_MODES_COUNT
}

static bool cmdParseLastStage(CmdParser *P,
                              CmdFlagValue *dst,
                              const char *str,
                              const char *name)
{
    static CmdEnumValueDesc stagesDesc[] = {
#define ff(NN, _) {#NN, ccs##NN},
        CXY_PUBLIC_COMPILER_STAGES(ff)
#undef ff
    };
    return cmdParseEnumValue(
        P, dst, str, name, stagesDesc, sizeof__(stagesDesc));
}

static bool cmdOptimizationLevel(CmdParser *P,
                                 CmdFlagValue *dst,
                                 const char *str,
                                 const char *name)
{
    if (str && str[0] != '\0' && str[1] == '\0') {
        dst->state = cmdNumber;
        switch (str[0]) {
        case '0':
        case 'd':
            dst->num = O0;
            return true;
        case '1':
            dst->num = O1;
            return true;
        case '2':
            dst->num = O2;
            return true;
        case '3':
            dst->num = O3;
            return true;
        default:
            dst->state = cmdNoValue;
            break;
        }
    }
    sprintf(P->error,
            "error: value '%s' passed to flag '%s' cannot be parsed as an "
            "optimization flag, supported values -O1, -O2, -O3 -Os -Od\n",
            str ?: "null",
            name);
    return false;
}

static bool cmdCompilerDefine(CmdParser *P,
                              CmdFlagValue *dst,
                              const char *str,
                              const char *name)
{
    if (str == NULL || str[0] == '\0') {
        sprintf(P->error,
                "error: command line argument '%s' is missing a value",
                name);
        return false;
    }
    if (dst->array.elems == NULL)
        dst->array = newDynArray(sizeof(CompilerDefine));

    const char *it = str;
    while (isspace(it[0]))
        it++;
    cstring variable = it, value = NULL;
    if (!isalpha(it[0]) && it[0] != '_') {
        sprintf(P->error,
                "error: define variable '%s' does not conform to cxy variable "
                "specification",
                name);
        return false;
    }

    while (isalnum(it[0]) || it[0] == '_')
        it++;
    if (it[0] == '=') {
        value = it + 1;
        ((char *)it)[0] = '\0';
    }
    else if (it[0] != '\0') {
        sprintf(P->error,
                "error: define variable '%s' does not conform to cxy variable "
                "specification",
                name);
        return false;
    }

    pushOnDynArray(&dst->array,
                   &(CompilerDefine){.name = variable, .value = value});
    return true;
}

static bool cmdArrayArgument(CmdParser *P,
                             CmdFlagValue *dst,
                             const char *str,
                             const char *name)
{
    if (str == NULL || str[0] == '\0') {
        sprintf(P->error,
                "error: command line argument '%s' is missing a value",
                name);
        return false;
    }
    if (dst->array.elems == NULL)
        dst->array = newDynArray(sizeof(cstring));

    pushStringOnDynArray(&dst->array, str);
    return true;
}

Command(
    dev,
    "development mode build, useful when debugging issues",
    Positionals(),
    Use(cmdParseLastStage,
        Name("last-stage"),
        Help("the last compiler stage to execute, e.g "
             "'Codegen'"),
        Def("Compile")),
    Use(cmdParseDumpAstModes,
        Name("dump-ast"),
        Help("Dumps the the AST as either JSON, YAML or CXY. Supported values: "
             "JSON|YAML|CXY"),
        Def("NONE")),
    Opt(Name("clean-ast"),
        Help("Prints the AST exactly as generated without any comments"),
        Def("false")),
    Opt(Name("with-location"),
        Help("Include the node location on the dumped AST"),
        Def("false")),
    Opt(Name("without-attrs"),
        Help("Exclude node attributes when dumping AST"),
        Def("false")),
    Opt(Name("with-named-enums"),
        Help("Use named enums on the when dumping AST"),
        Def("true")),
    Str(Name("output"),
        Sf('o'),
        Help("path to file to generate code or print AST to (default is "
             "stdout)"),
        Def("")),
    Opt(Name("print-ir"),
        Help("prints the generate IR (on supported backends, e.g LLVM)"),
        Def("false")),
    Opt(Name("emit-assembly"),
        Help("emits the generated assembly code to given filename on supported "
             "backends (e.g LLVM)"),
        Def("false")),
    Opt(Name("emit-bitcode"),
        Help("emits the generated bitcode to given filename on supported "
             "platforms (e.g LLVM)"),
        Def("false")));

Command(build,
        "transpiles the given cxy what file and builds it using gcc",
        Positionals(),
        Str(Name("output"),
            Sf('o'),
            Help("output file for the compiled binary (default: app)"),
            Def("app")),
        Str(Name("stdlib"),
            Help("root directory where cxy standard library is installed"),
            Def("")),
        Str(Name("build-dir"),
            Help("the build directory, used as the working directory for the "
                 "compiler"),
            Def("")));

// clang-format off
#define BUILD_COMMANDS(f)                                                      \
    PARSER_BUILTIN_COMMANDS(f)                                                 \
    f(dev)                                                                     \
    f(build)

#define DEV_CMD_LAYOUT(f, ...)                                                 \
    f(dev.lastStage, Local, Int, 0, ##__VA_ARGS__)                             \
    f(dev.dumpMode, Local, Int, 1, ##__VA_ARGS__)                              \
    f(dev.cleanAst, Local, Option, 2, ##__VA_ARGS__)                           \
    f(dev.withLocation, Local, Option, 3, ##__VA_ARGS__)                       \
    f(dev.withoutAttrs, Local, Option, 4, ##__VA_ARGS__)                       \
    f(dev.withNamedEnums, Local, Option, 5, ##__VA_ARGS__)                     \
    f(output, Local, String, 6, ##__VA_ARGS__)                                 \
    f(dev.printIR, Local, Option, 7, ##__VA_ARGS__)                            \
    f(dev.emitAssembly, Local, Option, 8, ##__VA_ARGS__)                       \
    f(dev.emitBitCode, Local, Option, 9, ##__VA_ARGS__)                        \

#define BUILD_CMD_LAYOUT(f, ...)                                               \
    f(output, Local, String, 0, ##__VA_ARGS__)                                 \
    f(libDir, Local, String, 1, ##__VA_ARGS__)                                 \
    f(buildDir, Local, String, 2, ##__VA_ARGS__)

// clang-format on

static void parseSpaceSeperatedList(DynArray *into,
                                    StrPool *strings,
                                    cstring str)
{
    if (str == NULL || str[0] == '\0')
        return;
    char *copy = (char *)makeString(strings, str);
    char *it = copy;
    do {
        while (*it && isspace(*it))
            it++;
        if (*it == '\0')
            break;
        pushStringOnDynArray(into, it);
        while (!isspace(*it) && *it)
            it++;
        if (*it == '\0')
            break;
        *it = '\0';
        it++;
    } while (true);
}

static void initializeOptions(StrPool *strings, Options *options)
{
    options->cflags = newDynArray(sizeof(char *));
    options->cDefines = newDynArray(sizeof(char *));
    options->librarySearchPaths = newDynArray(sizeof(char *));
    options->importSearchPaths = newDynArray(sizeof(char *));
    options->frameworkSearchPaths = newDynArray(sizeof(char *));
    options->libraries = newDynArray(sizeof(char *));
    options->defines = newDynArray(sizeof(CompilerDefine));
#ifdef __APPLE__
    pushOnDynArray(&options->defines, &(CompilerDefine){"MACOS", "1"});
    FormatState state = newFormatState(NULL, true);
    exec("xcrun --show-sdk-path", &state);
    char *sdkPath = formatStateToString(&state);
    freeFormatState(&state);
    if (sdkPath && sdkPath[0] != '\0') {
        cstring trimmedSdkPath = makeTrimmedString(strings, sdkPath);
        pushStringOnDynArray(&options->cflags, "-isysroot");
        pushStringOnDynArray(&options->cflags, trimmedSdkPath);
        free(sdkPath);

        state = newFormatState(NULL, true);
        appendString(&state, trimmedSdkPath);
        appendString(&state, "/usr/include");
        char *sdkIncludeDir = formatStateToString(&state);
        freeFormatState(&state);
        pushStringOnDynArray(&options->importSearchPaths,
                             makeString(strings, sdkIncludeDir));
        free(sdkIncludeDir);
    }
#endif
}

static void fixCmdDevOptions(Options *options)
{
    if (options->dev.emitBitCode) {
        options->dev.emitAssembly = false;
        options->dev.printIR = false;
        options->dev.dumpMode = dmpNONE;
        options->dev.lastStage = ccsCompile;
    }
    else if (options->dev.emitAssembly) {
        options->dev.printIR = false;
        options->dev.dumpMode = dmpNONE;
        options->dev.lastStage = ccsCompile;
    }
    else if (options->dev.printIR) {
        options->dev.dumpMode = dmpNONE;
        options->dev.lastStage = ccsCodegen;
    }
    else if (options->dev.dumpMode != dmpNONE) {
        options->dev.lastStage = MIN(options->dev.lastStage, ccsMemoryMgmt);
    }
}

static void moveListOptions(DynArray *dst, DynArray *src)
{
    copyDynArray(dst, src);
    freeDynArray(src);
}

bool parseCommandLineOptions(
    int *argc, char **argv, StrPool *strings, Options *options, Log *log)
{
    bool status = true;
    int file_count = 0;
    initializeOptions(strings, options);

    Parser(
        "cxy",
        CXY_VERSION,
        BUILD_COMMANDS,
        DefaultCmd(dev),
        Int(Name("max-errors"),
            Help(
                "Set the maximum number of errors incurred before the compiler "
                "aborts"),
            Def("10")),
        Str(Name("warnings"),
            Help("Sets a list of enabled/disabled compiler warning (eg "
                 "'~Warning' disables a flag, 'Warn1|~Warn2' combines flag "
                 "configurations))"),
            Def("")),
        Opt(Name("warnings-all"), Help("enables all compiler warnings")),
        Opt(Name("no-color"),
            Help("disable colored output when formatting outputs")),
        Opt(Name("without-builtins"),
            Help("Disable building builtins (does not for build command)")),
        Str(Name("cflags"),
            Help("C compiler flags to add to the c compiler when importing C "
                 "files"),
            Def("")),
        Opt(Name("no-pie"), Help("disable position independent code")),
        Use(cmdOptimizationLevel,
            Name("optimization"),
            Sf('O'),
            Help("Code optimization level, valid values '0', 'd', '1', '2', "
                 "'3', 's'"),
            Def("d")),
        Use(cmdCompilerDefine,
            Name("define"),
            Sf('D'),
            Help("Adds a compiler definition, e.g -DDISABLE_ASSERT, "
                 "-DAPP_VERSION=\\\"0.0.1\\\""),
            Def("[]")),
        Opt(Name("with-mm"),
            Help("Compile program with builtin (RC) memory manager"),
            Def("false")),
        Use(cmdArrayArgument,
            Name("c-define"),
            Help("Adds a compiler definition that will be parsed to the C "
                 "importer"),
            Def("[]")),
        Use(cmdArrayArgument,
            Name("c-header-dir"),
            Help("Adds a directory to search for C header files"),
            Def("[]")),
        Use(cmdArrayArgument,
            Name("c-lib-dir"),
            Help("Adds a directory to search for C libraries"),
            Def("[]")),
        Use(cmdArrayArgument,
            Name("c-lib"),
            Help("Adds library to link against"),
            Def("[]")),
        Opt(Sf('g'),
            Name("debug"),
            Help("Produce debug information for the program")),
        Opt(Name("debug-pass-manager"),
            Help("Print out information about LLVM executed passes"),
            Def("false")),
        Str(Name("passes"),
            Help("Describes a list of LLVM passes making up the pipeline"),
            Def("")),
        Use(cmdArrayArgument,
            Name("load-pass-plugin"),
            Help("Loads passes from the plugin library"),
            Def("[]")),
        Use(cmdParseDumpStatsModes,
            Name("dump-stats"),
            Help("Dump compilation statistics to console after compilation "
                 "values: NONE|SUMMARY|FULL"),
            Def("SUMMARY")),
        Opt(Name("no-progress"),
            Help("Do not print progress messages during compilation")));

    int selected = argparse(argc, &argv, parser);

    if (selected == CMD_help) {
        CmdFlagValue *cmd = cmdGetPositional(&help.meta, 0);
        cmdShowUsage(P, (cmd ? cmd->str : NULL), stdout);
        goto error;
    }
    else if (selected == -1) {
        logError(log, NULL, P->error, NULL);
        goto error;
    }

    CmdCommand *cmd = parser.cmds[selected];

    log->maxErrors = getGlobalInt(cmd, 0);
    log->ignoreStyles = getGlobalOption(cmd, 3);

    if (getGlobalOption(cmd, 2))
        log->enabledWarnings.num = wrnAll;
    log->enabledWarnings.str = getGlobalString(cmd, 1);
    log->enabledWarnings.num =
        parseWarningLevels(log, log->enabledWarnings.str);
    if (log->enabledWarnings.num & wrn_Error)
        return false;

    options->withoutBuiltins = getGlobalOption(cmd, 4);
    if (cmd->id == CMD_dev) {
        options->cmd = cmdDev;
        UnloadCmd(cmd, options, DEV_CMD_LAYOUT);
        fixCmdDevOptions(options);
    }
    else if (cmd->id == CMD_build) {
        options->cmd = cmdBuild;
        UnloadCmd(cmd, options, BUILD_CMD_LAYOUT);
    }

    cstring str = getGlobalString(cmd, 5);
    parseSpaceSeperatedList(&options->cflags, strings, str);
    options->noPIE = getGlobalOption(cmd, 6);
    options->optimizationLevel = getGlobalInt(cmd, 7);
    moveListOptions(&options->defines, &getGlobalArray(cmd, 8));
    options->withMemoryManager = getGlobalBool(cmd, 9);
    moveListOptions(&options->cDefines, &getGlobalArray(cmd, 10));
    moveListOptions(&options->importSearchPaths, &getGlobalArray(cmd, 11));
    moveListOptions(&options->librarySearchPaths, &getGlobalArray(cmd, 12));
    moveListOptions(&options->libraries, &getGlobalArray(cmd, 13));
    options->debug = getGlobalBool(cmd, 14);
    options->debugPassManager = getGlobalBool(cmd, 15);
    options->passes = getGlobalString(cmd, 16);
    moveListOptions(&options->loadPassPlugins, &getGlobalArray(cmd, 17));
    options->dsmMode = getGlobalInt(cmd, 18);
    log->progress = !getGlobalOption(cmd, 19);

    file_count = *argc - 1;

    if (file_count == 0) {
        logError(log,
                 NULL,
                 "no input file, run with '--help' to display usage",
                 NULL);
        goto error;
    }
    goto exit;

error:
    status = false;
exit:
    *argc = file_count + 1;
    return status;
}

void deinitCommandLineOptions(Options *options)
{
    freeDynArray(&options->cflags);
    freeDynArray(&options->cDefines);
    freeDynArray(&options->frameworkSearchPaths);
    freeDynArray(&options->importSearchPaths);
    freeDynArray(&options->librarySearchPaths);
    freeDynArray(&options->libraries);
    freeDynArray(&options->defines);
}
