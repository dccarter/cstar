//

#include "driver/options.h"
#include "driver/stages.h"

#include "core/args.h"
#include "core/log.h"

#include <math.h>
#include <stdio.h>

Command(dev,
        "development mode build, useful when developing the compiler",
        Positionals(),
        Str(Name("last-stage"),
            Help("the last compiler stage to execute, e.g "
                 "'Codegen'"),
            Def("Codegen")),
        Opt(Name("print-ast"),
            Help("prints the AST to standard output or given output file after "
                 "compilation"),
            Def("false")),
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
            Def("")));

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
    f(dev.printAst, Local, Option, 1, ##__VA_ARGS__)                           \
    f(dev.cleanAst, Local, Option, 2, ##__VA_ARGS__)                           \
    f(dev.withLocation, Local, Option, 3, ##__VA_ARGS__)                       \
    f(dev.withoutAttrs, Local, Option, 4, ##__VA_ARGS__)                       \
    f(dev.withNamedEnums, Local, Option, 5, ##__VA_ARGS__)                     \
    f(output, Local, String, 6, ##__VA_ARGS__)

#define BUILD_CMD_LAYOUT(f, ...)                                               \
    f(output, Local, String, 0, ##__VA_ARGS__)                                 \
    f(libDir, Local, String, 1, ##__VA_ARGS__)                                 \
    f(buildDir, Local, String, 2, ##__VA_ARGS__)

// clang-format on

bool parseCommandLineOptions(int *argc, char **argv, Options *options, Log *log)
{
    bool status = true;
    int file_count = 0;

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
            Help("disable colored output when formatting outputs")));

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
    log->state->ignoreStyle = getGlobalOption(cmd, 3);

    if (getGlobalOption(cmd, 2))
        log->enabledWarnings.num = wrnAll;
    log->enabledWarnings.str = getGlobalString(cmd, 1);
    log->enabledWarnings.num |=
        parseWarningLevels(log, log->enabledWarnings.str);
    if (log->enabledWarnings.num & wrn_Error)
        return false;

    if (cmd->id == CMD_dev) {
        UnloadCmd(cmd, options, DEV_CMD_LAYOUT);
        options->dev.lastStage.num =
            parseCompilerStages(log, options->dev.lastStage.str);
        if (options->dev.lastStage.num == ccsInvalid) {
            return false;
        }
        options->dev.lastStage.num = (u64)log2((f64)options->dev.lastStage.num);
    }
    else if (cmd->id == CMD_build) {
        options->cmd = cmdDev;
        UnloadCmd(cmd, options, BUILD_CMD_LAYOUT);
    }

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
