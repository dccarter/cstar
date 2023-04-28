/**
 * Credits: https://github.com/dccarter/cxy/blob/main/src/cxy/lang/parser.h
 */

#pragma once

#include "core/log.h"
#include "lang/token.h"

/*
 * The parser is LL(3), which means that it requires at most three tokens of
 * look-ahead. It is a simple recursive descent parser, implemented by hand,
 * which allocates nodes and strings on a memory pool.
 */

#define LOOK_AHEAD 3
#define TOKEN_BUFFER 4

typedef struct MemPool MemPool;
typedef struct StrPool StrPool;
typedef struct Lexer Lexer;
typedef struct AstNode AstNode;
typedef struct CompilerDriver CompilerDriver;

typedef struct {
    CompilerDriver *cc;
    Lexer *lexer;
    Log *L;
    MemPool *memPool;
    StrPool *strPool;
    bool inCase : 1;
    Token ahead[TOKEN_BUFFER];
} Parser;

Parser makeParser(Lexer *, CompilerDriver *);
AstNode *parseProgram(Parser *);
