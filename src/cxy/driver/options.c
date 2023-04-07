#include "driver/options.h"
#include "core/args.h"
#include "core/log.h"

#include <stdio.h>

Command(dev,
        "development mode build, useful when developing the compiler",
        Positionals(),
        Opt(Name("no-type-check"),
            Help("disable type checking, ignore if --print-ast is not set"),
            Def("false")),
        Opt(Name("print-ast"),
            Help("prints the AST to standard output or given output file after "
                 "compilation"),
            Def("false")),
        Str(Name("output"),
            Sf('o'),
            Help("path to file to generate code or print AST to (default is "
                 "stdout)"),
            Def("")));

Command(build,
        "transpiles the given cxy source file and builds it using gcc",
        Positionals(),
        Str(Name("output"),
            Sf('o'),
            Help("output file for the compile binary"),
            Def("")));

// clang-format off
#define BUILD_COMMANDS(f)                                                      \
    PARSER_BUILTIN_COMMANDS(f)                                                 \
    f(dev)                                                                     \
    f(build)

#define DEV_CMD_LAYOUT(f, ...)                                                 \
    f(noTypeCheck, Local, Option, 0, ##__VA_ARGS__)                            \
    f(printAst, Local, Option, 1, ##__VA_ARGS__)                               \
    f(output, Local, String, 2, ##__VA_ARGS__)

#define BUILD_CMD_LAYOUT(f, ...)                                               \
    f(output, Local, String, 0, ##__VA_ARGS__)

// clang-format on

bool parse_options(int *argc, char **argv, Options *options, Log *log)
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
    log->state->ignoreStyle = getGlobalOption(cmd, 1);

    if (cmd->id == CMD_dev) {
        UnloadCmd(cmd, options, DEV_CMD_LAYOUT);
    }
    else if (cmd->id == CMD_build) {
        options->cmd = cmdBuild;
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
