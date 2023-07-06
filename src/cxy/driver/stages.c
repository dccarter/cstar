//
// Created by Carter Mbotho on 2023-07-05.
//

#include "stages.h"

#include <ctype.h>
#include <string.h>

const char *getCompilerStageName(CompilerStages stage)
{
    switch (stage) {
#define f(NAME, ...)                                                           \
    case ccs##NAME:                                                            \
        return #NAME;
        CXY_COMPILER_STAGES(f)
#undef f
    default:
        return "<nothing>";
    }
}

const char *getCompilerStageDescription(CompilerStages stage)
{
    switch (stage) {
#define f(NAME, _, DESC)                                                       \
    case ccs##NAME:                                                            \
        return DESC;
        CXY_COMPILER_STAGES(f)
#undef f
    default:
        return "<nothing>";
    }
}

static CompilerStages parseNextCompilerStage(char *start, char *end)
{
    while (isspace(*start))
        start++;
    if (*start == '0')
        return ccsInvalid;

    u64 len;
    if (end) {
        while (isspace(*end))
            end--;
        end[1] = '\0';
        len = end - start;
    }
    else
        len = strlen(start);

    if (len < 5)
        return ccsInvalid;

    switch (start[0]) {
    case 'C':
        if (start[0] == 'o') {
            if (start[2] == 'd')
                return strcmp("egen", &start[3]) == 0 ? ccsCodegen : ccsInvalid;
            else if (start[2] == 'm')
                return strcmp("ptime", &start[3]) == 0 ? ccsComptime
                                                       : ccsInvalid;
            else if (start[2] == 'n')
                return strcmp("st", &start[3]) == 0 ? ccsConstCheck
                                                    : ccsInvalid;
        }
        return ccsInvalid;
    case 'D':
        return strcmp("esugar", &start[1]) == 0 ? ccsDesugar : ccsInvalid;
    case 'N':
        return strcmp("ameRes", &start[1]) == 0 ? ccsNameRes : ccsInvalid;

    case 'O':
        return strcmp("ptimization", &start[1]) == 0 ? ccsOptimization
                                                     : ccsInvalid;
    case 'T':
        return strcmp("ypeCheck", &start[1]) == 0 ? ccsTypeCheck : ccsInvalid;
    case 'M':
        return strcmp("emoryMgmt", &start[1]) == 0 ? ccsMemoryMgmt : ccsInvalid;
    default:
        break;
    }
}

CompilerStages parseCompilerStages(cstring str)
{
    CompilerStages stages = ccsInvalid;
    char *copy = strdup(str);
    char *start = copy, *end = strchr(str, '|');

    while (start) {
        char *last = end;
        if (last) {
            *last = '\0';
            last--;
            end++;
        }
        CompilerStages stage = parseNextCompilerStage(start, last);
        if (stage == ccsInvalid)
            return stage;
        stages |= stage;
        while (*end == '|')
            end++;

        last = end;
        end = strchr(last, '|');
    }

    free(copy);

    return stages;
}