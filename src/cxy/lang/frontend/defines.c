//
// Created by Carter Mbotho on 2024-03-12.
//
#include "defines.h"

#include "core/strpool.h"
#include "driver/driver.h"
#include <string.h>

#include "lexer.h"
#include "parser.h"

typedef struct {
    cstring variable;
    AstNode *value;
} PreprocessorMacro;

static bool comparePreprocessorMacro(const void *lhs, const void *rhs)
{
    return ((PreprocessorMacro *)lhs)->variable ==
           ((PreprocessorMacro *)rhs)->variable;
}

bool preprocessorOverrideDefinedMacro(struct CompilerPreprocessor *preprocessor,
                                      cstring variable,
                                      AstNode *value,
                                      AstNode **previous)
{
    HashCode hash = hashStr(hashInit(), variable);
    PreprocessorMacro *definition =
        findInHashTable(&preprocessor->symbols,
                        &(PreprocessorMacro){.variable = variable},
                        hash,
                        sizeof(PreprocessorMacro),
                        comparePreprocessorMacro);
    if (definition) {
        if (previous)
            *previous = definition->value;
        definition->value = value;
        return false;
    }

    bool status = insertInHashTable(&preprocessor->symbols,
                                    &(PreprocessorMacro){variable, value},
                                    hash,
                                    sizeof(PreprocessorMacro),
                                    comparePreprocessorMacro);
    csAssert0(status);
    return true;
}

bool preprocessorHasMacro(struct CompilerPreprocessor *preprocessor,
                          cstring variable,
                          AstNode **value)
{
    HashCode hash = hashStr(hashInit(), variable);
    PreprocessorMacro *definition =
        findInHashTable(&preprocessor->symbols,
                        &(PreprocessorMacro){.variable = variable},
                        hash,
                        sizeof(PreprocessorMacro),
                        comparePreprocessorMacro);
    if (definition) {
        if (value)
            *value = definition->value;
        return true;
    }
    return false;
}

void initCompilerPreprocessor(struct CompilerDriver *driver)
{
    Options *options = &driver->options;
    driver->preprocessor.symbols = newHashTable(sizeof(PreprocessorMacro));
    driver->preprocessor.pool = driver->pool;
    if (options->optimizationLevel > O0) {
        preprocessorDefineMacro(&driver->preprocessor,
                                makeString(driver->strings, "DISABLE_ASSERT"),
                                NULL);
    }

    for (int i = 0; i < options->defines.size; i++) {
        CompilerDefine *define =
            &dynArrayAt(CompilerDefine *, &options->defines, i);

        if (define->value == NULL) {
            preprocessorDefineMacro(&driver->preprocessor,
                                    makeString(driver->strings, define->name),
                                    NULL);
            continue;
        }

        Lexer lexer = newLexer(
            "defines", define->value, strlen(define->value), driver->L);
        Parser parser = makeParser(&lexer, driver);
        AstNode *node = parseExpression(&parser);
        preprocessorDefineMacro(&driver->preprocessor,
                                makeString(driver->strings, define->name),
                                node);
    }
}

void deinitCompilerPreprocessor(struct CompilerDriver *driver)
{
    if (driver->pool) {
        freeHashTable(&driver->preprocessor.symbols);
    }
}
