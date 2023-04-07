#include "driver/driver.h"
#include "core/log.h"
#include "core/mempool.h"
#include "core/utils.h"
#include "driver/options.h"
#include "lang/ast.h"
#include "lang/check.h"
#include "lang/codegen.h"
#include "lang/lexer.h"
#include "lang/parser.h"
#include "lang/ttable.h"

#include <errno.h>
#include <unistd.h>

static AstNode *parseFile(const char *fileName, MemPool *memPool, Log *log)
{
    size_t file_size = 0;
    char *fileData = readFile(fileName, &file_size);
    if (!fileData) {
        logError(log,
                 NULL,
                 "cannot open file '{s}'",
                 (FormatArg[]){{.s = fileName}});
        return NULL;
    }

    Lexer lexer = newLexer(fileName, fileData, file_size, log);
    Parser parser = makeParser(&lexer, memPool);
    AstNode *program = parseProgram(&parser);

    freeLexer(&lexer);
    free(fileData);

    return program;
}

void invokeCCompiler(const char *fileName, const Options *options, Log *log)
{
    char cmd[512];
    sprintf(cmd, "cc %s -O3 -o app", options->output);
    system(cmd);
}

bool compileFile(const char *fileName, const Options *options, Log *log)
{
    MemPool memPool = newMemPool();
    StrPool strPool = newStrPool(&memPool);
    AstNode *program = parseFile(fileName, &memPool, log);
    FILE *output = stdout;
    TypeTable *table = newTypeTable(&memPool, &strPool);

    if (options->cmd == cmdBuild ||
        (!options->noTypeCheck && log->errorCount == 0)) {
        semanticsCheck(program, log, &memPool, &strPool, table);
    }

    if (options->output) {
        output = fopen(options->output, "w");
        if (output == NULL) {
            logError(log,
                     NULL,
                     "creating output file '{s}' failed, does directory exist?",
                     (FormatArg[]){{.s = options->output}});
        }
    }

    if (options->printAst && log->errorCount == 0) {
        FormatState state = newFormatState(
            "    ", log->state->ignoreStyle || !isColorSupported(stdout));
        printAst(&state, program);
        writeFormatState(&state, output);
        freeFormatState(&state);
        printf("\n");
        return program;
    }

    if (log->errorCount == 0) {
        FormatState state = newFormatState("  ", true);
        generateCode(&state, table, &strPool, program);
        writeFormatState(&state, output);
        freeFormatState(&state);
        fclose(output);

        if (options->cmd == cmdBuild) {
            invokeCCompiler(fileName, options, log);
        }
    }

    freeTypeTable(table);

    return program;
}
