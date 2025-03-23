#include "lexer.h"
#include "core/alloc.h"
#include "core/hash.h"
#include "core/utils.h"
#include "token.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    size_t len;
    TokenTag tag;
} Keyword;

static bool compareKeywords(const void *left, const void *right)
{
    return ((Keyword *)left)->len == ((Keyword *)right)->len &&
           !memcmp(((Keyword *)left)->name,
                   ((Keyword *)right)->name,
                   ((Keyword *)left)->len);
}

static void registerKeywords(HashTable *keywords)
{
#define f(name, str, ...)                                                      \
    insertInHashTable(keywords,                                                \
                      &(Keyword){str, strlen(str), tok##name},                 \
                      hashStr(hashInit(), str),                                \
                      sizeof(Keyword),                                         \
                      compareKeywords);
    KEYWORD_LIST(f)
#undef f
}

static LexerBuffer *newLexerBuffer(const char *fileName,
                                   const char *fileData,
                                   size_t fileSize)
{
    LexerBuffer *buffer = mallocOrDie(sizeof(LexerBuffer));
    buffer->fileName = fileName;
    buffer->fileData = fileData;
    buffer->fileSize = fileSize;
    buffer->filePos = (FilePos){.row = 1, .col = 1};
    buffer->ownData = false;
    buffer->prev = NULL;
    return buffer;
}

static void deleteLexerBuffer(LexerBuffer *b)
{
    if (b->ownData) {
        free((void *)b->fileData);
    }
    free(b);
}

static void deleteLexerBufferChain(LexerBuffer *b)
{
    while (b) {
        LexerBuffer *tmp = b;
        b = b->prev;
        deleteLexerBuffer(tmp);
    }
}
static void popBuffer(Lexer *L)
{
    if (L->buffer && L->buffer->prev) {
        LexerBuffer *b = L->buffer;
        L->buffer = b->prev;
        b->prev = L->cleanup;
        L->cleanup = b;
    }
}

Lexer newLexer(const char *fileName,
               const char *fileData,
               size_t fileSize,
               Log *log)
{
    enum {
#define f(name, str, ...) KEYWORD_##name,
        KEYWORD_LIST(f)
#undef f
            KEYWORD_COUNT
    };
    Lexer lexer = {.log = log,
                   .buffer = newLexerBuffer(fileName, fileData, fileSize),
                   .keywords =
                       newHashTableWithCapacity(KEYWORD_COUNT, sizeof(Keyword)),
                   .flags = lxNone};
    registerKeywords(&lexer.keywords);
    return lexer;
}

static bool isEofReached(const Lexer *lexer)
{
    return lexer->buffer->fileSize <= lexer->buffer->filePos.byteOffset;
}

static const char *getCurPtr(const Lexer *lexer)
{
    // assert(!isEofReached(lexer));
    static const char EoF = EOF;
    return isEofReached(lexer)
               ? &EoF
               : &lexer->buffer->fileData[lexer->buffer->filePos.byteOffset];
}

static char getCurChar(Lexer *lexer) { return *getCurPtr(lexer); }

static char peekNextChar_(const LexerBuffer *b)
{
    if (b->fileSize == (1 + b->filePos.byteOffset)) {
        if (b->prev) {
            return peekNextChar_(b->prev);
        }
        return EOF;
    }
    return b->fileData[b->filePos.byteOffset + 1];
}

static inline char peekNextChar(const Lexer *lexer)
{
    return peekNextChar_(lexer->buffer);
}

static void skipChar(Lexer *lexer)
{
    if (getCurChar(lexer) == '\n') {
        lexer->buffer->filePos.row++;
        lexer->buffer->filePos.col = 1;
    }
    else
        lexer->buffer->filePos.col++;
    lexer->buffer->filePos.byteOffset++;
    if (isEofReached(lexer) && lexer->buffer->prev)
        popBuffer(lexer);
}

static inline bool isSpaceOrPunctuation(char c)
{
    return c != '_' && (isspace(c) || ispunct(c));
}

static void skipUntilSpaceOrPunctuation(Lexer *lexer)
{
    while (!isSpaceOrPunctuation(getCurChar(lexer)))
        advanceLexer(lexer);
}

static bool acceptChar(Lexer *lexer, char c)
{
    if (!isEofReached(lexer)) {
        if (getCurChar(lexer) == c) {
            skipChar(lexer);
            return true;
        }
    }
    return false;
}

static void skipSpaces(Lexer *lexer)
{
    while (!isEofReached(lexer) && isspace(getCurChar(lexer)))
        skipChar(lexer);
}

static void skipSingleLineComment(Lexer *lexer)
{
    while (!isEofReached(lexer) && getCurChar(lexer) != '\n')
        skipChar(lexer);
}

static bool skipMultiLineComment(Lexer *lexer)
{
    u64 nest = 1;
    while (nest > 0 && !isEofReached(lexer)) {
        if (acceptChar(lexer, '/')) {
            if (acceptChar(lexer, '*'))
                nest++;
            continue;
        }
        if (acceptChar(lexer, '*')) {
            if (acceptChar(lexer, '/'))
                nest--;
            continue;
        }
        skipChar(lexer);
    }
    return nest == 0;
}

static inline Token makeToken(Lexer *lexer, const FilePos *begin, TokenTag tag)
{
    return (Token){
        .tag = tag,
        .fileLoc = {.fileName = lexer->buffer->fileName,
                    .begin = *begin,
                    .end = lexer->buffer->filePos},
        .buffer = lexer->buffer,
    };
}

static inline Token makeToken_(Lexer *lexer,
                               LexerBuffer *buffer,
                               const FilePos *begin,
                               TokenTag tag)
{
    return (Token){.tag = tag,
                   .fileLoc = {.fileName = lexer->buffer->fileName,
                               .begin = *begin,
                               .end = buffer->filePos},
                   .buffer = buffer};
}

static Token makeIntLiteral(Lexer *lexer, const FilePos *begin, uintmax_t iVal)
{
    Token token = makeToken(lexer, begin, tokIntLiteral);
    token.iVal = iVal;
    return token;
}

static Token makeFloatLiteral(Lexer *lexer, const FilePos *begin, double fVal)
{
    Token token = makeToken(lexer, begin, tokFloatLiteral);
    token.fVal = fVal;
    return token;
}

static Token makeInvalidToken(Lexer *lexer,
                              const FilePos *begin,
                              const char *errMsg)
{
    Token token = makeToken(lexer, begin, tokError);
    logError(lexer->log, &token.fileLoc, errMsg, NULL);
    return token;
}

static Token multilineString(Lexer *lexer)
{
    FilePos begin = lexer->buffer->filePos;
    LexerBuffer *buffer = lexer->buffer;
    skipChar(lexer);
    while (!isEofReached(lexer)) {
        if (acceptChar(lexer, '"')) {
            FilePos end = lexer->buffer->filePos;
            if (getCurChar(lexer) == '"' && peekNextChar(lexer) == '"') {
                skipChar(lexer);
                skipChar(lexer);
                return (Token){.fileLoc = {.fileName = lexer->buffer->fileName,
                                           .begin = begin,
                                           .end = end},
                               .tag = tokStringLiteral,
                               .buffer = buffer};
            }
        }
        else {
            skipChar(lexer);
        }
    }
    return makeInvalidToken(lexer, &begin, "unterminated multiline string");
}

Token advanceLexer(Lexer *lexer)
{
    if (lexer->flags & lxExitStringExpr) {
        lexer->flags &= ~lxExitStringExpr;
        return makeToken(lexer, &lexer->buffer->filePos, tokRString);
    }

    if (lexer->flags & lxReturnLStrFmt) {
        lexer->flags &= ~lxReturnLStrFmt;
        return makeToken(lexer, &lexer->buffer->filePos, tokLStrFmt);
    }

    if (lexer->flags & lxMaybeNotFloat) {
        char next = getCurChar(lexer);
        if (!isdigit(next) && next != '.')
            lexer->flags &= ~lxMaybeNotFloat;
    }

    while (true) {
        bool parsingStringLiteral = false;
        FilePos begin = lexer->buffer->filePos;
        LexerBuffer *buffer = NULL;
        if (lexer->flags & lxEnterStringExpr) {
            lexer->flags &= ~lxEnterStringExpr;
            lexer->flags |= lxContinueStringExpr;
            skipChar(lexer);
            buffer = lexer->buffer;
            goto lexerLexString;
        }

        skipSpaces(lexer);
        begin = lexer->buffer->filePos;

        while (isEofReached(lexer) && lexer->buffer->prev) {
            popBuffer(lexer);
            skipSpaces(lexer);
        }
        if (isEofReached(lexer))
            return makeToken(lexer, &begin, tokEoF);
        buffer = lexer->buffer;

        if (acceptChar(lexer, '('))
            return makeToken(lexer, &begin, tokLParen);
        if (acceptChar(lexer, ')'))
            return makeToken(lexer, &begin, tokRParen);
        if (acceptChar(lexer, '[')) {
            return makeToken(lexer, &begin, tokLBracket);
        }
        if (acceptChar(lexer, ']'))
            return makeToken(lexer, &begin, tokRBracket);
        if (acceptChar(lexer, '{'))
            return makeToken(lexer, &begin, tokLBrace);
        if (acceptChar(lexer, '.')) {
            if (acceptChar(lexer, '.')) {
                if (acceptChar(lexer, '.')) {
                    return makeToken(lexer, &begin, tokElipsis);
                }
                return makeToken(lexer, &begin, tokDotDot);
            }
            if (acceptChar(lexer, '[')) {
                return makeToken(lexer, &begin, tokIndexExpr);
            }
            lexer->flags |= lxMaybeNotFloat;
            return makeToken(lexer, &begin, tokDot);
        }
        if (acceptChar(lexer, ','))
            return makeToken(lexer, &begin, tokComma);
        if (acceptChar(lexer, ':'))
            return makeToken(lexer, &begin, tokColon);
        if (acceptChar(lexer, ';'))
            return makeToken(lexer, &begin, tokSemicolon);
        if (acceptChar(lexer, '#')) {
            if (acceptChar(lexer, '{'))
                return makeToken(lexer, &begin, tokSubstitutue);
            if (acceptChar(lexer, '.'))
                return makeToken(lexer, &begin, tokAstMacroAccess);
            if (acceptChar(lexer, '#'))
                return makeToken(lexer, &begin, tokDefine);
            return makeToken(lexer, &begin, tokHash);
        }
        if (acceptChar(lexer, '`'))
            return makeToken(lexer, &begin, tokQuote);
        if (acceptChar(lexer, '@'))
            return makeToken(lexer, &begin, tokAt);
        if (acceptChar(lexer, '?'))
            return makeToken(lexer, &begin, tokQuestion);
        if (acceptChar(lexer, '}')) {
            if (lexer->flags & lxContinueStringExpr)
                goto lexerLexString;
            return makeToken(lexer, &begin, tokRBrace);
        }

        if (acceptChar(lexer, '!')) {
            if (acceptChar(lexer, '='))
                return makeToken(lexer, &begin, tokNotEqual);
            if (acceptChar(lexer, ':'))
                return makeToken(lexer, &begin, tokBangColon);
            return makeToken(lexer, &begin, tokLNot);
        }

        if (acceptChar(lexer, '~')) {
            return makeToken(lexer, &begin, tokBNot);
        }

        if (acceptChar(lexer, '+')) {
            if (acceptChar(lexer, '+'))
                return makeToken(lexer, &begin, tokPlusPlus);
            if (acceptChar(lexer, '='))
                return makeToken(lexer, &begin, tokPlusEqual);
            return makeToken(lexer, &begin, tokPlus);
        }

        if (acceptChar(lexer, '-')) {
            if (acceptChar(lexer, '-'))
                return makeToken(lexer, &begin, tokMinusMinus);
            if (acceptChar(lexer, '='))
                return makeToken(lexer, &begin, tokMinusEqual);
            if (acceptChar(lexer, '>'))
                return makeToken(lexer, &begin, tokThinArrow);
            return makeToken(lexer, &begin, tokMinus);
        }

        if (acceptChar(lexer, '*')) {
            if (acceptChar(lexer, '='))
                return makeToken(lexer, &begin, tokMultEqual);
            return makeToken(lexer, &begin, tokMult);
        }

        if (acceptChar(lexer, '%')) {
            if (acceptChar(lexer, '='))
                return makeToken(lexer, &begin, tokModEqual);
            return makeToken(lexer, &begin, tokMod);
        }

        if (acceptChar(lexer, '&')) {
            if (acceptChar(lexer, '&'))
                return makeToken(lexer, &begin, tokLAnd);
            if (acceptChar(lexer, '='))
                return makeToken(lexer, &begin, tokBAndEqual);
            if (acceptChar(lexer, '.'))
                return makeToken(lexer, &begin, tokBAndDot);
            return makeToken(lexer, &begin, tokBAnd);
        }

        if (acceptChar(lexer, '|')) {
            if (acceptChar(lexer, '|'))
                return makeToken(lexer, &begin, tokLOr);
            if (acceptChar(lexer, '='))
                return makeToken(lexer, &begin, tokBOrEqual);
            return makeToken(lexer, &begin, tokBOr);
        }

        if (acceptChar(lexer, '^')) {
            if (acceptChar(lexer, '='))
                return makeToken(lexer, &begin, tokBXorEqual);
            return makeToken(lexer, &begin, tokBXor);
        }

        if (acceptChar(lexer, '<')) {
            if (acceptChar(lexer, '<')) {
                if (acceptChar(lexer, '='))
                    return makeToken(lexer, &begin, tokShlEqual);
                return makeToken(lexer, &begin, tokShl);
            }
            if (acceptChar(lexer, '='))
                return makeToken(lexer, &begin, tokLessEqual);
            return makeToken(lexer, &begin, tokLess);
        }

        if (acceptChar(lexer, '>')) {
            if (acceptChar(lexer, '>')) {
                if (acceptChar(lexer, '='))
                    return makeToken(lexer, &begin, tokShrEqual);
                return makeToken(lexer, &begin, tokShr);
            }
            if (acceptChar(lexer, '='))
                return makeToken(lexer, &begin, tokGreaterEqual);
            return makeToken(lexer, &begin, tokGreater);
        }

        if (acceptChar(lexer, '=')) {
            if (acceptChar(lexer, '='))
                return makeToken(lexer, &begin, tokEqual);
            if (acceptChar(lexer, '>'))
                return makeToken(lexer, &begin, tokFatArrow);
            return makeToken(lexer, &begin, tokAssign);
        }

        if (acceptChar(lexer, '/')) {
            if (acceptChar(lexer, '/')) {
                skipSingleLineComment(lexer);
                continue;
            }
            else if (acceptChar(lexer, '*')) {
                if (skipMultiLineComment(lexer))
                    continue;
                else
                    return makeInvalidToken(
                        lexer, &begin, "unterminated block comment");
            }
            if (acceptChar(lexer, '='))
                return makeToken(lexer, &begin, tokDivEqual);
            return makeToken(lexer, &begin, tokDiv);
        }

        if (acceptChar(lexer, '\"')) {
            if (getCurChar(lexer) == '\"' && peekNextChar(lexer) == '\"') {
                skipChar(lexer);
                return multilineString(lexer);
            }

            parsingStringLiteral = true;
        lexerLexString:
            while (getCurChar(lexer) != '\"') {
                if (acceptChar(lexer, '\n')) {
                    // Backslash to continue string on another line
                    if (acceptChar(lexer, '\\'))
                        continue;
                    break;
                }

                if (getCurChar(lexer) == '\\') {
                    // accept all escape sequences
                    skipChar(lexer);
                    skipChar(lexer);
                    continue;
                }

                if (!parsingStringLiteral &&
                    (lexer->flags & lxContinueStringExpr)) {
                    if (getCurChar(lexer) == '$' &&
                        peekNextChar(lexer) == '{') {
                        skipChar(lexer); // skip $
                        Token tok =
                            makeToken_(lexer, buffer, &begin, tokStringLiteral);
                        lexer->flags |= lxReturnLStrFmt;
                        skipChar(lexer); // skip {
                        return tok;
                    }
                }
                skipChar(lexer);
            }

            if (!parsingStringLiteral) {
                lexer->flags &= ~lxContinueStringExpr;
                lexer->flags |= lxExitStringExpr;
            }
            if (!acceptChar(lexer, '\"'))
                return makeInvalidToken(
                    lexer, &begin, "unterminated string literal");
            return makeToken_(lexer, buffer, &begin, tokStringLiteral);
        }

        if (acceptChar(lexer, '\'')) {
            const char *ptr = getCurPtr(lexer);
            size_t charCount = 0;
            for (; getCurChar(lexer) != '\'' && getCurChar(lexer) != '\n';
                 charCount++) {
                if (getCurChar(lexer) == '\\') {
                    skipChar(lexer);
                    charCount++;
                }
                skipChar(lexer);
            }

            Token token = makeToken(lexer, &begin, tokCharLiteral);
            if (!acceptChar(lexer, '\'') ||
                convertEscapeSeq(
                    ptr, getCurPtr(lexer) - ptr - 1, &token.cVal) != charCount)
                return makeInvalidToken(
                    lexer, &begin, "invalid character literal");
            return token;
        }

        if ((getCurChar(lexer) == 'f') && peekNextChar(lexer) == '"') {
            skipChar(lexer);
            if (lexer->flags & (lxEnterStringExpr | lxContinueStringExpr))
                return makeInvalidToken(
                    lexer, &begin, "unsupported nesting of string expressions");
            lexer->flags |= lxEnterStringExpr;
            return makeToken(lexer, &begin, tokLString);
        }

        begin = buffer->filePos;
        if (getCurChar(lexer) == '_' || isalpha(getCurChar(lexer))) {
            skipChar(lexer);
            while (getCurChar(lexer) == '_' || isalnum(getCurChar(lexer)))
                skipChar(lexer);
            const char *name = buffer->fileData + begin.byteOffset;
            size_t len = buffer->filePos.byteOffset - begin.byteOffset;
            Keyword *keyword =
                findInHashTable(&lexer->keywords,
                                &(Keyword){.name = name, .len = len},
                                hashRawBytes(hashInit(), name, len),
                                sizeof(Keyword),
                                compareKeywords);
            return keyword ? makeToken(lexer, &begin, keyword->tag)
                           : makeToken_(lexer, buffer, &begin, tokIdent);
        }

        if (isdigit(getCurChar(lexer))) {
            bool wasZero = getCurChar(lexer) == '0';
            const char *ptr = getCurPtr(lexer);

            skipChar(lexer);
            if (wasZero) {
                if (acceptChar(lexer, 'b')) {
                    // Binary literal
                    ptr = getCurPtr(lexer);
                    while (getCurChar(lexer) == '0' || getCurChar(lexer) == '1')
                        skipChar(lexer);
                    if (!isSpaceOrPunctuation(getCurChar(lexer))) {
                        skipUntilSpaceOrPunctuation(lexer);
                        return makeInvalidToken(
                            lexer,
                            &begin,
                            "binary digit contains invalid characters");
                    }

                    return makeIntLiteral(
                        lexer, &begin, strtoumax(ptr, NULL, 2));
                }
                else if (acceptChar(lexer, 'x')) {
                    // Hexadecimal literal
                    ptr = getCurPtr(lexer);
                    while (isxdigit(getCurChar(lexer)))
                        skipChar(lexer);

                    if (!isSpaceOrPunctuation(getCurChar(lexer))) {
                        skipUntilSpaceOrPunctuation(lexer);
                        return makeInvalidToken(
                            lexer,
                            &begin,
                            "hexadecimal digit contains invalid characters");
                    }

                    return makeIntLiteral(
                        lexer, &begin, strtoumax(ptr, NULL, 16));
                }
                else if (acceptChar(lexer, 'o')) {
                    // Octal literal
                    ptr = getCurPtr(lexer);
                    while (getCurChar(lexer) >= '0' && getCurChar(lexer) <= '7')
                        skipChar(lexer);
                    if (!isSpaceOrPunctuation(getCurChar(lexer))) {
                        skipUntilSpaceOrPunctuation(lexer);
                        return makeInvalidToken(
                            lexer,
                            &begin,
                            "octal digit contains invalid characters");
                    }
                    return makeIntLiteral(
                        lexer, &begin, strtoumax(ptr, NULL, 8));
                }
            }

            // Parse integral part
            while (isdigit(getCurChar(lexer)))
                skipChar(lexer);

            bool hasDot = false;
            if (getCurChar(lexer) == '.' && peekNextChar(lexer) != '.' &&
                peekNextChar(lexer) != '[' &&
                !(lexer->flags & lxMaybeNotFloat)) {
                skipChar(lexer);
                hasDot = true;
                // Parse fractional part
                while (isdigit(getCurChar(lexer)))
                    skipChar(lexer);
                // Parse exponent
                if (acceptChar(lexer, 'e')) {
                    // Accept `+`/`-` signs
                    if (!acceptChar(lexer, '+'))
                        acceptChar(lexer, '-');
                    while (isdigit(getCurChar(lexer)))
                        skipChar(lexer);
                }
            }

            return hasDot ? makeFloatLiteral(lexer, &begin, strtod(ptr, NULL))
                          : makeIntLiteral(
                                lexer, &begin, strtoumax(ptr, NULL, 10));
        }

        skipChar(lexer);
        return makeInvalidToken(lexer, &begin, "invalid token");
    }
}

void freeLexer(Lexer *lexer)
{
    deleteLexerBufferChain(lexer->buffer);
    deleteLexerBufferChain(lexer->cleanup);
    lexer->buffer = NULL;
    lexer->cleanup = NULL;
    freeHashTable(&lexer->keywords);
}

void lexerPush(Lexer *L, const char *fileName)
{
    LexerBuffer *buffer = mallocOrDie(sizeof(LexerBuffer));
    buffer->fileName = fileName;
    buffer->filePos = (FilePos){.row = 1, .col = 1};
    buffer->fileData = readFile(fileName, &buffer->fileSize);
    buffer->ownData = true;
    buffer->prev = L->buffer;
    L->buffer = buffer;
}

bool isKeyword(TokenTag tag)
{
    switch (tag) {
#define f(T, ...) case tok##T:
        KEYWORD_LIST(f)
#undef f
        return true;
    default:
        return false;
    }
}
