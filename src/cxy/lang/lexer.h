// Credits https://github.com/madmann91/fu/blob/master/src/fu/lang/lexer.h
#pragma once

#include "core/htable.h"
#include "core/log.h"
#include "lang/token.h"

/*
 * The lexer requires to have the entire file data in memory (or a memory mapped
 * file, if needs be), and produces tokens one at a time. The file data must be
 * terminated by a null character.
 */

enum {
    lxNone = 0,
    lxEnterStringExpr = BIT(0),
    lxContinueStringExpr = BIT(1),
    lxExitStringExpr = BIT(2)
};

typedef struct Lexer {
    const char *fileName;
    const char *fileData;
    size_t fileSize;
    FilePos filePos;
    Log *log;
    HashTable keywords;
    u32 flags;
} Lexer;

Lexer newLexer(const char *fileName,
               const char *fileData,
               size_t fileSize,
               Log *);

void freeLexer(Lexer *);

Token advanceLexer(Lexer *);
