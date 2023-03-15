#include "driver/driver.h"
#include "core/log.h"
#include "core/mempool.h"
#include "core/utils.h"
#include "driver/options.h"
#include "lang/ast.h"
#include "lang/lexer.h"
#include "lang/parser.h"

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

bool compileFile(const char *fileName, const Options *options, Log *log)
{
    MemPool memPool = newMemPool();
    AstNode *program = parseFile(fileName, &memPool, log);

    if (options->printAst) {
        FormatState state = newFormatState(
            "    ", log->state->ignoreStyle || !isColorSupported(stdout));
        printAst(&state, program);
        writeFormatState(&state, stdout);
        freeFormatState(&state);
        printf("\n");
    }

    return program;
}
