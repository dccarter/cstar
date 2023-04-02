/**
 * Copyright (c) 2022 Suilteam, Carter Mbotho
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Carter
 * @date 2022-07-21
 */

#include "args.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/ioctl.h>
#endif

#if !defined(__unix__) && !defined(__APPLE__)
attr(always_inline)
#endif
    static u32 cmdTerminalColumns(FILE *fp)
{
#if defined(__unix__) || defined(__APPLE__)

    struct winsize ws;
    int fd = fileno(fp);
    if (fd >= 0 && ioctl(fd, TIOCGWINSZ, &ws) == 0)
        return MAX(80, ws.ws_col);
#endif
    return 80;
}

static bool cmdAddIndented(
    FILE *fp, u32 ident, u32 col, const char *text, u32 added)
{
    if (col < 20)
        return false;
    if (text == NULL || text[0] == '\0')
        return true;

    if (added == 0)
        fprintf(fp, "%*c", ident, ' ');
    else if (added == -1)
        added = 0;

    for (u32 i = ident; (text != NULL && text[0] != '\0');) {
        const char *sp = strchr(text, ' ');
        const u32 len = (sp != NULL) ? (++sp - text) : strlen(text);
        if (i + len > (col - added)) {
            // jump to the next line
            fputc('\n', fp);
            return cmdAddIndented(fp, ident, col, text, 0);
        }
        fwrite(text, sizeof(text[0]), len, fp);
        i += len;
        text = sp;
    }

    return true;
}

attr(always_inline) static bool cmdIsLongFlag(const char *s)
{
    return (s[0] == '-' && s[1] == '-' && s[2] != '\0');
}

attr(always_inline) static bool cmdIsShortFlag(const char *s)
{
    return (s[0] == '-' && s[1] != '-' && s[1] != '\0');
}

attr(always_inline) static bool cmdIsIgnoreArguments(const char *s)
{
    return (s[0] == '-' && s[1] == '-' && s[2] == '\0');
}

static CmdFlag *cmdFindByName(CmdFlag *args,
                              u32 nargs,
                              const char *name,
                              u32 len)
{
    for (int i = 0; i < nargs; i++) {
        if (args[i].name == NULL)
            continue;
        if (strncmp(args[i].name, name, len) == 0)
            return &args[i];
    }

    return NULL;
}

static CmdFlag *cmdFindByShortFormat(CmdFlag *args, u32 nargs, char sf)
{
    for (int i = 0; i < nargs; i++) {
        if (args[i].sf == '\0')
            continue;
        if (args[i].sf == sf)
            return &args[i];
    }

    return NULL;
}

static CmdCommand *cmdFindCommandByName(CmdParser *P, const char *name)
{
    if (name == NULL)
        return NULL;
    if (name[0] == '\0')
        return P->def;

    for (int i = 0; i < P->ncmds; i++) {
        if (P->cmds[i]->name == NULL)
            continue;
        if (strcmp(P->cmds[i]->name, name) == 0)
            return P->cmds[i];
    }

    return NULL;
}

static void cmdShowCommandFlag(
    const CmdFlag *arg, u16 la, u32 cols, FILE *fp, bool skipStar)
{
    u32 ident;
    i32 defLen = -1;
    int spaces = MAX(0, la - strlen(arg->name)) + 2;
    ident = spaces;
    spaces += (arg->sf == '\0') ? 4 : 0;
    ident += 4;

    fprintf(fp, "%*c", spaces, ' ');

    if (!skipStar) {
        if (arg->validator && arg->def == NULL)
            fputc('*', fp);
        else
            fputc(' ', fp);
        ident++;
    }

    if (arg->sf != '\0')
        fprintf(fp, "-%c, ", arg->sf);
    fprintf(fp, "--%s", arg->name);
    ident += strlen(arg->name) + 2;

    if (arg->validator)
        fputc('=', fp);
    else
        fputc(' ', fp);
    fputc(' ', fp);
    ident += 2;

    if (arg->def && arg->def[0] != '\0') {
        fprintf(fp, "(Default: %s) ", arg->def);
        defLen = (i32)strlen(arg->def) + 12;
    }
    if (arg->help)
        cmdAddIndented(fp, ident, cols, arg->help, defLen);
    fputc('\n', fp);
}

static void cmdShowCommandUsage(const CmdParser *P,
                                const CmdCommand *cmd,
                                u32 cols,
                                FILE *fp)
{
    fprintf(fp, "Usage: %s %s", P->name, cmd->name);
    if (cmd->nargs || P->nargs)
        fputs(" [options]", fp);
    if (cmd->npos)
        fputs(" ...", fp);
    fputs("\n\n", fp);

    if (cmd->npos) {
        fputs("Command inputs (in order):\n", fp);
        for (int i = 0; i < cmd->npos; i++) {
            CmdPositional *pos = &cmd->pos[i];
            int spaces = MAX(0, cmd->lp - strlen(pos->name)) + 2;
            u32 ident = spaces;
            i32 defLen = -1;

            fprintf(fp, "%*c", spaces, ' ');
            if (pos->validator && pos->def == NULL)
                fputc('*', fp);
            else
                fputc(' ', fp);
            fputs(pos->name, fp);
            fputc(' ', fp);
            ident += strlen(pos->name) + 2;

            if (pos->def && pos->def[0] != '\0') {
                fprintf(fp, "(Default: %s) ", pos->def);
                defLen = (i32)strlen(pos->def) + 12;
            }

            if (pos->help)
                cmdAddIndented(fp, ident, cols, pos->help, defLen);
            fputc('\n', fp);
        }
    }
    if (cmd->nargs) {
        fputs("\nCommand options:\n", fp);
        for (int i = 0; i < cmd->nargs; i++) {
            cmdShowCommandFlag(&cmd->args[i], cmd->la, cols, fp, false);
        }
    }
}

static void vmInitializeCommand(CmdCommand *cmd, CmdFlag *gArgs, u32 ngArgs)
{
    for (int i = 0; i < cmd->nargs; i++) {
        CmdFlag *arg = &cmd->args[i];
        memset(&arg->val, 0, sizeof(arg->val));

        for (int j = i + 1; j < cmd->nargs; j++) {
            if (strcmp(arg->name, cmd->args[j].name) == 0) {
                fprintf(stderr,
                        "Duplicate duplicate flag name '%s' at index %d and %d "
                        "on command '%s'!\n",
                        arg->name,
                        i,
                        j,
                        cmd->name);
                abort();
            }

            if (arg->sf != '\0' && arg->sf == cmd->args[j].sf) {
                fprintf(stderr,
                        "Duplicate flag short format '%c' detected at index %d "
                        "and %d on command '%s'!\n",
                        arg->sf,
                        i,
                        j,
                        cmd->name);
                abort();
            }
        }

        for (int j = 0; j < ngArgs; j++) {
            if (strcmp(arg->name, gArgs[j].name) == 0) {
                fprintf(stderr,
                        "Flag name '%s' defined in command '%s' at index %d "
                        "already used as global flag at index %d!\n",
                        arg->name,
                        cmd->name,
                        i,
                        j);
                abort();
            }

            if (arg->sf != '\0' && (arg->sf == gArgs[j].sf)) {
                fprintf(stderr,
                        "Flag short format '%c' defined in '%s' at index %d "
                        "already used as global flag at index %d!\n",
                        arg->sf,
                        arg->name,
                        i,
                        j);
                abort();
            }
        }

        cmd->la = MAX(cmd->la, strlen(cmd->args[i].name));
    }

    for (int i = 0; i < cmd->npos; i++) {
        memset(&cmd->pos[i].val, 0, sizeof(CmdFlagValue));
        cmd->lp = MAX(cmd->lp, strlen(cmd->pos[i].name));
    }
}

static bool cmdParseCommandArguments(CmdParser *P,
                                     CmdCommand *cmd,
                                     int *pargc,
                                     char ***pargv)
{
    int argc = *pargc;
    char **argv = *pargv;
    u32 npos = 0;
    int reWrite = 0;

    for (; argc > 0; --argc, ++argv) {
        const char *arg = *argv;
        const char *og = arg;
        const char *val = NULL;
        CmdFlag *flag;

        if (cmdIsIgnoreArguments(arg)) {
            --argc;
            ++argv;
            break;
        }

        if (cmdIsShortFlag(arg)) {
            arg++;
            flag = cmdFindByShortFormat(P->args, P->nargs, arg[0]);
            if (flag == NULL)
                flag = cmdFindByShortFormat(cmd->args, cmd->nargs, arg[0]);
            arg++;
            if (arg[0] != '\0')
                val = arg;
        }
        else if (cmdIsLongFlag(arg)) {
            u32 len;
            arg += 2;
            len = strlen(arg);
            const char *eq = strchr(arg, '=');
            if (eq) {
                val = eq + 1;
                while (isspace(*--eq))
                    ;
                len = eq - arg;
                while (isspace(*val))
                    val++;
            }
            flag = cmdFindByName(P->args, P->nargs, arg, len);
            if (flag == NULL)
                flag = cmdFindByName(cmd->args, cmd->nargs, arg, len);
        }
        else {
            if (cmd->npos == 0) {
                (*pargv)[reWrite++] = (char *)arg;
            }
            else if (npos >= cmd->npos) {
                sprintf(cmd->P->error,
                        "error: unexpected number of positional arguments "
                        "passed, expecting %u\n",
                        cmd->npos);
                return false;
            }
            else {
                CmdPositional *pos = &cmd->pos[npos];
                if (!pos->validator) {
                    pos->val.state = cmdString;
                    pos->val.str = arg;
                }
                else if (!pos->validator(P, &pos->val, arg, pos->name)) {
                    return false;
                }
            }
            npos++;
            continue;
        }

        // parse argument value if any
        if (flag == NULL) {
            sprintf(cmd->P->error,
                    "error: unrecognized command argument '%s'\n",
                    og);
            return false;
        }

        if (flag->validator) {
            if (val == NULL && argc > 0) {
                val = *++argv;
                --argc;
            }
            if (val == NULL) {
                sprintf(cmd->P->error,
                        "error: command flag '%s' expects a value\n",
                        flag->name);
                return false;
            }
            if (!flag->validator(P, &flag->val, val, flag->name)) {
                return false;
            }
        }
        else if (val != NULL) {
            sprintf(cmd->P->error,
                    "error: unexpected value passed to flag '%s'\n",
                    flag->name);
            return false;
        }
        else {
            // flag that doesn't take a value
            flag->val.state = cmdNumber;
            flag->val.num = 1;
        }
    }

    *pargc = reWrite + 1;
    *pargv = argv;
    return true;
}

static void cmdHandleBuiltins(CmdCommand *cmd, bool def)
{
    CmdParser *P = cmd->P;
    CmdFlagValue *version = cmdGetGlobalFlag(cmd, 0);
    CmdFlagValue *help = cmdGetGlobalFlag(cmd, 1);
    if (version && version->num) {
        fprintf(stdout, "%s v%s\n", P->name, P->version);
        exit(EXIT_SUCCESS);
    }
    if (help && help->num) {
        cmdShowUsage(P, def ? NULL : cmd->name, stdout);
        exit(EXIT_SUCCESS);
    }
}

bool cmdParseString(attr(unused) CmdParser *P,
                    CmdFlagValue *dst,
                    const char *str,
                    attr(unused) const char *name)
{
    dst->state = cmdString;
    dst->str = str;
    return true;
}

bool cmdParseBoolean(CmdParser *P,
                     CmdFlagValue *dst,
                     const char *str,
                     const char *name)
{
    u32 len = strlen(str);
    if (len == 1) {
        if (str[0] == '0' || str[0] == '1') {
            dst->num = str[0] == '1';
        }
        else
            goto parseBoolError;
    }
    else if (len == 4 && strcasecmp(str, "true") == 0) {
        dst->num = 1;
    }
    else if (len == 5 && strcasecmp(str, "false") == 0) {
        dst->num = 0;
    }
    else
        goto parseBoolError;

    dst->state = cmdNumber;
    return true;

parseBoolError:
    sprintf(
        P->error,
        "error: value '%s' passed to flag '%s' cannot be parsed as a boolean\n",
        str,
        name);
    return false;
}

bool cmdParseDouble(CmdParser *P,
                    CmdFlagValue *dst,
                    const char *str,
                    const char *name)
{
    char *end;
    dst->num = strtod(str, &end);
    if (errno == ERANGE || *end != '\0') {
        sprintf(P->error,
                "error: value '%s' passed to flag '%s' cannot be parsed as a "
                "double\n",
                str,
                name);
        return false;
    }
    dst->state = cmdNumber;
    return true;
}

bool cmdParseInteger(CmdParser *P,
                     CmdFlagValue *dst,
                     const char *str,
                     const char *name)
{
    char *end;
    dst->num = (double)strtoll(str, &end, 0);
    if (errno == ERANGE || *end != '\0') {
        sprintf(P->error,
                "error: value '%s' passed to flag '%s' cannot be parsed as a "
                "number\n",
                str,
                name);
        return false;
    }

    dst->state = cmdNumber;
    return true;
}

bool cmdParseByteSize(CmdParser *P,
                      CmdFlagValue *dst,
                      const char *str,
                      const char *name)
{
    char *end;
    dst->num = (double)strtoull(str, &end, 0);
    if (errno == ERANGE) {
        sprintf(P->error,
                "error: value '%s' passed to flag '%s' cannot be parsed as a "
                "number\n",
                str,
                name);
        return false;
    }
    if (end) {
        if (end[0] != '\0' && end[1] != '\0')
            goto unsupportedDataSizeUnit;

        switch (end[0]) {
        case 'g':
        case 'G':
            dst->num *= 1024 * 1024 * 1024;
            break;
        case 'm':
        case 'M':
            dst->num *= 1024 * 1024;
            break;
        case 'k':
        case 'K':
            dst->num *= 1024;
            break;
        case 'b':
        case 'B':
        case '\0':
            break;
        default:
        unsupportedDataSizeUnit:
            sprintf(P->error,
                    "'%s' is not a supported data size unit "
                    "(use g,G for gigabytes, m,M for megabytes, "
                    "k,K for kilobytes or b, B for bytes)",
                    end);
            return false;
        }
    }

    dst->state = cmdNumber;
    return true;
}

bool cmdParseBitFlags(CmdParser *P,
                      CmdFlagValue *dst,
                      const char *str,
                      const char *name,
                      CmdBitFlagDesc *bitFlags,
                      u32 count)
{
    u32 value = 0;
    u32 sizes[count];

    for (int i = 0; i < count; i++)
        sizes[i] = strlen(bitFlags[i].name);

    while (str) {
        bool parsed = false;
        const char *next;
        u32 size = 0;
        while (*str && (*str == '|' || isspace(*str)))
            str++;
        next = strchr(str, '|');

        if (next != NULL) {
            const char *e = next - 1;
            while (e != str && isspace(*e))
                e--;
            if (e == str)
                continue;
            size = e - str + 1;
        }
        else {
            size = strlen(str);
        }

        for (int i = 0; i < count; i++) {
            if (size == sizes[i] &&
                strncasecmp(str, bitFlags[i].name, size) == 0) {
                value |= bitFlags[i].value;
                parsed = true;
                break;
            }
        }

        if (!parsed) {
            int n = sprintf(
                P->error,
                "'%.*s' is not a supported bit flags for argument '%s'\n",
                size,
                str,
                name);
            return false;
        }

        str = next;
    }
    dst->num = value;
    dst->state = cmdNumber;
    return true;
}

CmdFlagValue *cmdGetFlag(CmdCommand *cmd, u32 i)
{
    if (i >= cmd->nargs || cmd->args[i].val.state == cmdNoValue)
        return NULL;
    return &cmd->args[i].val;
}

CmdFlagValue *cmdGetGlobalFlag(CmdCommand *cmd, u32 i)
{
    CmdParser *P = cmd->P;
    if (P == NULL || i >= P->nargs || P->args[i].val.state == cmdNoValue)
        return NULL;
    return &P->args[i].val;
}

CmdFlagValue *cmdGetPositional(CmdCommand *cmd, u32 i)
{
    if (i >= cmd->npos || cmd->pos[i].val.state == cmdNoValue)
        return NULL;
    return &cmd->pos[i].val;
}

void cmdShowUsage(CmdParser *P, const char *name, FILE *fp)
{
    u32 cols = cmdTerminalColumns(fp);
    CmdCommand *cmd = cmdFindCommandByName(P, name);
    if (cmd != NULL)
        cmdShowCommandUsage(P, cmd, cols, fp);
    else {
        fprintf(fp, "Usage: %s [cmd] ...\n\n", P->name);
        fputs("Commands:\n", fp);
        for (int i = 0; i < P->ncmds; i++) {
            cmd = P->cmds[i];
            int spaces = MAX(0, P->lc - strlen(cmd->name)) + 2;
            u32 ident = spaces;
            fprintf(fp, "%*c", spaces, ' ');
            if (cmd == P->def)
                fputc('*', fp);
            else
                fputc(' ', fp);
            ident++;

            fputs(cmd->name, fp);
            fputc(' ', fp);
            ident += strlen(cmd->name) + 1;
            if (cmd->help)
                cmdAddIndented(fp, ident, cols, cmd->help, -1);
            fputc('\n', fp);
        }
    }
    if (P->nargs) {
        fputs("\nGlobal Flags:\n", fp);
        for (int i = 0; i < P->nargs; i++) {
            const CmdFlag *arg = &P->args[i];
            if (arg->isAppOnly && cmd != NULL)
                continue;
            cmdShowCommandFlag(arg, P->la, cols, fp, true);
        }
    }
}

i32 parseCommandLineArguments_(int *pargc, char ***pargv, CmdParser *P)
{
    int argc = *pargc - 1;
    char **argv = *pargv + 1;

    for (int i = 0; i < P->nargs; i++) {
        CmdFlag *arg = &P->args[i];
        memset(&arg->val, 0, sizeof(arg->val));

        for (int j = i + 1; j < P->nargs; j++) {
            if (strcmp(arg->name, P->args[j].name) == 0) {
                fprintf(stderr,
                        "Duplicate flag name'%s' detected at index %d and %d! "
                        "on global flags!\n",
                        arg->name,
                        i,
                        j);
                abort();
            }

            if (arg->sf != '\0' && arg->sf == P->args[j].sf) {
                fprintf(stderr,
                        "Duplicate flag short format '%c' detected at index %d "
                        "and %d! on global flags!\n",
                        arg->sf,
                        i,
                        j);
                abort();
            }
        }

        P->la = MAX(P->la, strlen(P->args[i].name));
    }

    for (int i = 0; i < P->ncmds; i++) {
        // command names must be unique (slow but I don't suppose one will add
        // 100 commands)
        for (int j = i + 1; j < P->ncmds; j++) {
            if (strcmp(P->cmds[i]->name, P->cmds[j]->name) == 0) {
                fprintf(stderr,
                        "Duplicate command name '%s'  detected at index %d and "
                        "%d!\n",
                        P->cmds[i]->name,
                        i,
                        j);
                abort();
            }
        }
        P->lc = MAX(P->lc, strlen(P->cmds[i]->name));
        P->cmds[i]->P = P;
        vmInitializeCommand(P->cmds[i], P->args, P->nargs);
    }

    CmdCommand *cmd = P->def;
    bool choseDef = true;
    if (argc) {
        if (argv[0][0] != '-') {
            if ((cmd = cmdFindCommandByName(P, argv[0]))) {
                argv++;
                argc--;
                choseDef = false;
            }
            else
                cmd = P->def;
        }
    }

    if (cmd == NULL) {
        sprintf(P->error, "error: missing required command to execute\n");
        return -1;
    }

    if (!cmdParseCommandArguments(P, cmd, &argc, &argv))
        return -1;

    cmdHandleBuiltins(cmd, choseDef);

    // Validate that all require flags and positional arguments have been set
    for (int i = 0; i < cmd->nargs; i++) {
        CmdFlag *flag = &cmd->args[i];
        if (flag->val.state != cmdNoValue)
            continue;
        if (flag->validator == NULL) {
            // flags that do not have a validator are considered optional
            flag->val.state = cmdNumber;
            continue;
        }

        if (flag->def != NULL) {
            if (flag->def[0] == '\0') {
                flag->val.state = cmdString;
                flag->val.str = NULL;
            }
            else if (!flag->validator(P, &flag->val, flag->def, flag->name))
                return -1;
        }
        else {
            sprintf(P->error,
                    "Missing flag '%s' required by command '%s'\n",
                    flag->name,
                    cmd->name);
            return -1;
        }
    }

    for (int i = 0; i < P->nargs; i++) {
        CmdFlag *flag = &P->args[i];
        if (flag->val.state != cmdNoValue)
            continue;
        if (flag->validator == NULL) {
            // flags that do not have a validator are considered optional
            flag->val.state = cmdNumber;
            continue;
        }

        if (flag->def != NULL) {
            if (flag->def[0] != '\0' &&
                !flag->validator(P, &flag->val, flag->def, flag->name))
                return -1;
        }
        else {
            sprintf(P->error,
                    "Missing flag '%s' required by command '%s'\n",
                    flag->name,
                    cmd->name);
            return -1;
        }
    }

    for (int i = 0; i < cmd->npos; i++) {
        CmdPositional *pos = &cmd->pos[i];
        if (pos->val.state != cmdNoValue)
            continue;
        if (pos->validator == NULL)
            // flag that do not have a validator are considered optional
            continue;
        if (pos->def != NULL) {
            if (pos->def[0] != '\0' &&
                !pos->validator(P, &pos->val, pos->def, pos->name))
                return -1;
        }
        else {
            sprintf(P->error,
                    "Missing positional argument %d ('%s') required by command "
                    "'%s'\n",
                    i,
                    pos->name,
                    cmd->name);
            return -1;
        }
    }

    *pargc = argc;
    *pargv = argv;

    return cmd->id;
}
