/**
 * Copyright (c) 2022 Suilteam, Carter Mbotho
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Carter
 * @date 2022-07-21
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <core/array.h>
#include <core/utils.h>
#include <stdio.h>

enum { cmdNoValue, cmdNumber, cmdString, cmdArray };

struct CommandLineParser;

typedef struct CommandLineArgumentValue {
    u8 state;
    union {
        f64 num;
        const char *str;
        DynArray array;
    };
} CmdFlagValue;

typedef struct CommandLineEnumValueDesc {
    const char *str;
    f64 value;
} CmdEnumValueDesc;

typedef struct CommandLineArgument {
    const char *name;
    char sf;
    const char *help;
    const char *def;
    CmdFlagValue val;
    bool isAppOnly;
    bool (*validator)(struct CommandLineParser *,
                      CmdFlagValue *,
                      const char *,
                      const char *);
} CmdFlag;

typedef struct CommandLinePositional {
    const char *name;
    const char *help;
    const char *def;
    CmdFlagValue val;
    bool (*validator)(struct CommandLineParser *,
                      CmdFlagValue *,
                      const char *,
                      const char *);
} CmdPositional;

typedef struct CommandLineCommand {
    const char *name;
    const char *help;
    u32 nargs;
    u32 npos;
    CmdPositional *pos;
    struct CommandLineParser *P;
    u16 lp;
    u16 la;
    i32 id;
    CmdFlag *args;
} CmdCommand;

typedef struct CommandLineParser {
    const char *name;
    const char *version;
    const char *describe;
    CmdCommand *def;
    u16 la;
    u16 lc;
    u32 ncmds;
    u32 nargs;
    CmdCommand **cmds;
    CmdFlag *args;
    char error[128];
} CmdParser;

typedef struct CmdBitFlagDesc {
    const char *name;
    u32 value;
} CmdBitFlagDesc;

bool cmdParseString(CmdParser *P,
                    CmdFlagValue *dst,
                    const char *str,
                    const char *name);
bool cmdParseBoolean(CmdParser *P,
                     CmdFlagValue *dst,
                     const char *str,
                     const char *name);
bool cmdParseDouble(CmdParser *P,
                    CmdFlagValue *dst,
                    const char *str,
                    const char *name);
bool cmdParseInteger(CmdParser *P,
                     CmdFlagValue *dst,
                     const char *str,
                     const char *name);
bool cmdParseByteSize(CmdParser *P,
                      CmdFlagValue *dst,
                      const char *str,
                      const char *name);

bool cmdParseEnumValue(CmdParser *P,
                       CmdFlagValue *dst,
                       const char *str,
                       const char *name,
                       const CmdEnumValueDesc *enums,
                       u32 len);

bool cmdParseBitFlags(CmdParser *P,
                      CmdFlagValue *dst,
                      const char *str,
                      const char *name,
                      CmdBitFlagDesc *bitFlags,
                      u32 len);

#define Name(N) .name = N
#define Sf(S) .sf = S
#define Help(H) .help = H
#define Type(V) .validator = V
#define Def(D) .def = D
#define BindArray(Arr) .val.array = Arr
#define Positionals(...)                                                       \
    {                                                                          \
        __VA_ARGS__                                                            \
    }
#define Opt(...)                                                               \
    {                                                                          \
        __VA_ARGS__, .validator = NULL                                         \
    }
#define Str(...)                                                               \
    {                                                                          \
        __VA_ARGS__, .validator = cmdParseString                               \
    }
#define Int(...)                                                               \
    {                                                                          \
        __VA_ARGS__, .validator = cmdParseInteger                              \
    }
#define Bool(...)                                                              \
    {                                                                          \
        __VA_ARGS__, .validator = cmdParseBoolean                              \
    }
#define Float(...)                                                             \
    {                                                                          \
        __VA_ARGS__, .validator = cmdParseDouble                               \
    }
#define Bytes(...)                                                             \
    {                                                                          \
        __VA_ARGS__, .validator = cmdParseByteSize                             \
    }

#define Use(V, ...)                                                            \
    {                                                                          \
        __VA_ARGS__, .validator = V                                            \
    }

#define Sizeof(T, ...) (sizeof((T[]){__VA_ARGS__}) / sizeof(T))
#define Command(N, H, P, ...)                                                  \
    static int CMD_##N = 0;                                                    \
    typedef struct Cmd##N Cmd##N;                                              \
    static struct Cmd##N {                                                     \
        CmdCommand meta;                                                       \
        CmdFlag args[Sizeof(CmdFlag, __VA_ARGS__)];                            \
        CmdPositional pos[sizeof((CmdPositional[])P) / sizeof(CmdPositional)]; \
    } N = {.meta = {.name = #N,                                                \
                    .help = H,                                                 \
                    .nargs = Sizeof(CmdFlag, __VA_ARGS__),                     \
                    .npos =                                                    \
                        (sizeof((CmdPositional[])P) / sizeof(CmdPositional))}, \
           .args = {__VA_ARGS__},                                              \
           .pos = P}

#define Cmd(N) &((N).meta)

CmdFlagValue *cmdGetFlag(CmdCommand *cmd, u32 i);
CmdFlagValue *cmdGetGlobalFlag(CmdCommand *cmd, u32 i);
CmdFlagValue *cmdGetPositional(CmdCommand *cmd, u32 i);
void cmdShowUsage(CmdParser *P, const char *name, FILE *fp);
i32 parseCommandLineArguments_(int *pargc, char ***pargv, CmdParser *P);

#define RequireCmd NULL
#define DefaultCmd(C) Cmd(C)

#define __FLATTEN_COMMAND(FC) &(FC).meta,
#define __FLATTEN_COMMANDS(COMMANDS)                                           \
    {                                                                          \
        COMMANDS(__FLATTEN_COMMAND)                                            \
    }

#define __INIT_COMMAND(N)                                                      \
    {                                                                          \
        CMD_##N = cmdCOUNT++;                                                  \
        (N).meta.id = CMD_##N;                                                 \
        (N).meta.args = (N).args;                                              \
        (N).meta.pos = (N).pos;                                                \
        &((N).meta);                                                           \
    }

#define __INIT_COMMANDS(COMMANDS) COMMANDS(__INIT_COMMAND)

#define CMDL_HELP_CMD                                                          \
    Command(                                                                   \
        help,                                                                  \
        "Get the application or help related to a specific command",           \
        Positionals(Str(Name("command"),                                       \
                        Help("The command whose help should be retrieved"),    \
                        Def(""))));

#define PARSER_BUILTIN_COMMANDS(f) f(help)

#define Parser(N, V, CMDS, DEF, ...)                                           \
    CMDL_HELP_CMD                                                              \
    int cmdCOUNT = 0;                                                          \
    struct {                                                                   \
        CmdParser P;                                                           \
        CmdCommand *cmds[(sizeof((CmdCommand *[])__FLATTEN_COMMANDS(CMDS)) /   \
                          sizeof(CmdCommand *))];                              \
        CmdFlag args[2 + Sizeof(CmdFlag, __VA_ARGS__)];                        \
    } parser = {                                                               \
        .P = {.name = N,                                                       \
              .version = V,                                                    \
              .def = DEF,                                                      \
              .ncmds = (sizeof((CmdCommand *[])__FLATTEN_COMMANDS(CMDS)) /     \
                        sizeof(CmdCommand *)),                                 \
              .nargs = 2 + Sizeof(CmdFlag, __VA_ARGS__)},                      \
        .cmds = __FLATTEN_COMMANDS(CMDS),                                      \
        .args = {Opt(Name("version"),                                          \
                     Sf('v'),                                                  \
                     Help("Show the application version"),                     \
                     .isAppOnly = true),                                       \
                 Opt(Name("help"),                                             \
                     Sf('h'),                                                  \
                     Help("Get help for the selected command")),               \
                 ##__VA_ARGS__}};                                              \
    __INIT_COMMANDS(CMDS)                                                      \
    CmdParser *P = &parser.P

#define argparse(ARGC, ARGV, PP)                                               \
    ({                                                                         \
        (PP).P.cmds = (PP).cmds;                                               \
        (PP).P.args = (PP).args;                                               \
        parseCommandLineArguments_((ARGC), (ARGV), &(PP).P);                   \
    })

#define getGlobalInt(cmd, I) (int)cmdGetGlobalFlag(cmd, (I) + 2)->num
#define getGlobalFloat(cmd, I) cmdGetGlobalFlag(cmd, (I) + 2)->num
#define getGlobalBytes(cmd, I) (int)cmdGetGlobalFlag(cmd, (I) + 2)->num
#define getGlobalOption(cmd, I) (cmdGetGlobalFlag(cmd, (I) + 2)->num != 0)
#define getGlobalBool(cmd, I) (cmdGetGlobalFlag(cmd, (I) + 2)->num != 0)
#define getGlobalString(cmd, I) cmdGetGlobalFlag(cmd, (I) + 2)->str
#define getGlobalArray(cmd, I) cmdGetGlobalFlag(cmd, (I) + 2)->array
#define getLocalInt(cmd, I) (int)cmdGetFlag(cmd, (I))->num
#define getLocalFloat(cmd, I) cmdGetFlag(cmd, (I))->num
#define getLocalBytes(cmd, I) (int)cmdGetFlag(cmd, (I))->num
#define getLocalOption(cmd, I) (cmdGetFlag(cmd, (I))->num != 0)
#define getLocalBool(cmd, I) (cmdGetFlag(cmd, (I))->num != 0)
#define getLocalString(cmd, I) cmdGetFlag(cmd, (I))->str
#define getLocalArray(cmd, I) cmdGetFlag(cmd, (I))->array
#define getPositionalInt(cmd, I) (int)cmdGetPositional(cmd, (I))->num
#define getPositionalFloat(cmd, I) cmdGetPositional(cmd, (I))->num
#define getPositionalBytes(cmd, I) (int)cmdGetPositional(cmd, (I))->num
#define getPositionalBool(cmd, I) (cmdGetPositional(cmd, (I))->num != 0)
#define getPositionalString(cmd, I) cmdGetPositional(cmd, (I))->str
#define getPositionalArray(cmd, I) cmdGetPositional(cmd, (I))->array

#define __UNLOAD_TO_TARGET_WITH(cmd, target, name, G, I)                       \
    (target)->name = G(cmd, I);
#define __UNLOAD_TO_TARGET(name, LOC, T, I, cmd, target)                       \
    __UNLOAD_TO_TARGET_WITH(cmd, target, name, CXY_PASTE_XYZ(get, LOC, T), I)

#define UnloadCmd(cmd, target, LAYOUT) LAYOUT(__UNLOAD_TO_TARGET, cmd, target)

#ifdef __cplusplus
}
#endif
