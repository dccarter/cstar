//
// Created by Carter Mbotho on 2024-02-02.
//

extern "C" {
#include "lang/frontend/lexer.h"
#include "lang/frontend/parser.h"
#include "lang/middle/operations.h"
}

#include "driver.hpp"

namespace cxy {

Compiler::Compiler()
    : generalMemPool{newMemPool()}, stringsMemPool{newMemPool()},
      strings{newStrPool(&stringsMemPool)}, L{newLog(NULL, NULL)}
{
    config.pool = &generalMemPool;
    config.strings = &strings;
}

bool Compiler::compile(cstring fileName)
{
    size_t fileSize = 0;
    char *fileData = readFile(fileName, &fileSize);
    if (!fileData) {
        logError(
            &L, NULL, "cannot open file '{s}'", (FormatArg[]){{.s = fileName}});
        return false;
    }

    Lexer lexer = newLexer(fileName, fileData, fileSize, &L);
    Parser parser = makeParser(&lexer, &config);
    AstNode *program = parseProgram(&parser);
    if (program) {
        FormatState state = newFormatState("  ", false);
        dumpCxySource(&state, program);
        writeFormatState(&state, stdout);
        freeFormatState(&state);
    }
    freeLexer(&lexer);
    free(fileData);
    return !hasErrors(&L);
}
} // namespace cxy