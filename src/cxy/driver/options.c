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

static EnumOptionMap dumpModes[] = {
#define ff(NN) {#NN, dmp##NN},
    DUMP_OPTIONS(ff)
#undef ff
};
#define DUMP_OPTIONS_COUNT (sizeof(dumpModes) / sizeof(EnumOptionMap))

Command(
    dev,
    "development mode build, useful when debugging issues",
    Positionals(),
    Str(Name("last-stage"),
        Help("the last compiler stage to execute, e.g "
             "'Codegen'"),
        Def("Codegen")),
    Str(Name("dump-ast"),
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
        Def("false")));

Command(build,
        "transpiles the given cxy what file and builds it using gcc",
        Positionals(),
        Str(Name("output"),
            Sf('o'),
            Help("output file for the compiled binary (default: app)"),
            Def("app")),
        Str(Name("lib"),
            Sf('L'),
            Help("root directory where cxy standard library is installed"),
            Def("")),
        Str(Name("build-dir"),
            Help("the build directory, used as the working directory for the "
                 "compiler"),
            Def(".build")));

// clang-format off
#define BUILD_COMMANDS(f)                                                      \
    PARSER_BUILTIN_COMMANDS(f)                                                 \
    f(dev)                                                                     \
    f(build)

#define DEV_CMD_LAYOUT(f, ...)                                                 \
    f(dev.lastStage.str, Local, String, 0, ##__VA_ARGS__)                      \
    f(dev.dump, Local, String, 1, ##__VA_ARGS__)                                \
    f(dev.cleanAst, Local, Option, 2, ##__VA_ARGS__)                           \
    f(dev.withLocation, Local, Option, 3, ##__VA_ARGS__)                       \
    f(dev.withoutAttrs, Local, Option, 4, ##__VA_ARGS__)                       \
    f(dev.withNamedEnums, Local, Option, 5, ##__VA_ARGS__)                     \
    f(output, Local, String, 6, ##__VA_ARGS__)                                 \
    f(dev.printIR, Local, Option, 7, ##__VA_ARGS__)

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
    options->ldflags = newDynArray(sizeof(char *));
    options->defines = newDynArray(sizeof(char *));
#ifdef __APPLE__
    pushStringOnDynArray(&options->defines, "MACOS");
    FormatState state = newFormatState(NULL, true);
    exec("xcrun --show-sdk-path", &state);
    char *sdkPath = formatStateToString(&state);
    if (sdkPath && sdkPath[0] != '\0') {
        cstring trimmedSdkPath = makeTrimmedString(strings, sdkPath);
        pushStringOnDynArray(&options->cflags, "-isysroot");
        pushStringOnDynArray(&options->cflags, trimmedSdkPath);
        free(sdkPath);
    }
#endif
}

static u64 parseEnumOption(
    Log *L, cstring str, EnumOptionMap *values, u64 len, u64 def)
{
    while (*str == '\0')
        str++;
    u64 size = 0;
    while (str[size])
        size++;

    for (u64 i = 0; i < len; i++) {
        if (strncmp(str, values[i].name, size) == 0)
            return values[i].value;
    }

    return def;
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
            Def("None")),
        Opt(Name("warnings-all"), Help("enables all compiler warnings")),
        Opt(Name("no-color"),
            Help("disable colored output when formatting outputs")),
        Opt(Name("without-builtins"),
            Help("Disable building builtins (does not for build command)")),
        Str(Name("cflags"),
            Help("C compiler flags to add to the c compiler when importing C "
                 "files"),
            Def("")));

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
    log->enabledWarnings.num |=
        parseWarningLevels(log, log->enabledWarnings.str);
    if (log->enabledWarnings.num & wrn_Error)
        return false;

    options->withoutBuiltins = getGlobalOption(cmd, 4);
    if (cmd->id == CMD_dev) {
        UnloadCmd(cmd, options, DEV_CMD_LAYOUT);
        options->dev.lastStage.num =
            parseCompilerStages(log, options->dev.lastStage.str);
        if (options->dev.lastStage.num == ccsInvalid) {
            return false;
        }
        options->dev.lastStage.num = (u64)log2((f64)options->dev.lastStage.num);

        // parse dump mode
        options->dev.dumpMode = parseEnumOption(
            log, options->dev.dump, dumpModes, DUMP_OPTIONS_COUNT, dmpNONE);
    }
    else if (cmd->id == CMD_build) {
        options->cmd = cmdBuild;
        UnloadCmd(cmd, options, BUILD_CMD_LAYOUT);
    }

    cstring str = getGlobalString(cmd, 5);
    parseSpaceSeperatedList(&options->cflags, strings, str);

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
