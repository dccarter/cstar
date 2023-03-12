// credits: https://github.com/madmann91/fu/blob/master/src/fu/lang/token.h
#pragma once

#include <core/utils.h>
#include <core/log.h>
#include <lang/types.h>

// clang-format off

#define SYMBOL_LIST(f)              \
    f(LParen, "(")                  \
    f(RParen, ")")                  \
    f(LBracket, "[")                \
    f(RBracket, "]")                \
    f(LBrace, "{")                  \
    f(RBrace, "}")                  \
    f(Hash, "#")                    \
    f(Bang, "!")                    \
    f(Dot, ".")                     \
    f(DotDot, "..")                 \
    f(Elipsis, "...")               \
    f(Comma, ",")                   \
    f(Colon, ":")                   \
    f(Semicolon, ";")               \
    f(Assign, "=")                  \
    f(Equal, "==")                  \
    f(NotEqual, "!=")               \
    f(FatArrow, "=>")               \
    f(ThinArrow, "->")              \
    f(Less, "<")                    \
    f(LessEqual, "<=")              \
    f(Shl, "<<")                    \
    f(ShlEqual, "<<=")              \
    f(Greater, ">")                 \
    f(GreaterEqual, ">=")           \
    f(Shr, ">>")                    \
    f(ShrEqual, ">>=")              \
    f(Plus, "+")                    \
    f(Minus, "-")                   \
    f(Mult, "*")                    \
    f(Div, "/")                     \
    f(Mod, "%")                     \
    f(BAnd, "&")                    \
    f(BXor, "^")                    \
    f(BOr, "|")                     \
    f(LAnd, "&&")                   \
    f(LOr, "||")                    \
    f(Plusplus, "++")               \
    f(MinusMinus, "--")             \
    f(PlusEqual, "+=")              \
    f(MinusEqual, "-=")             \
    f(MultEqual, "*=")              \
    f(DivEqual, "/=")               \
    f(ModEqual, "%=")               \
    f(BAndEqual, "&=")              \
    f(BXorEqual, "^=")              \
    f(BorEqual, "|=")

#define KEYWORD_LIST(f)             \
    f(True, "true")                 \
    f(False, "false")               \
    f(If, "if")                     \
    f(Else, "else")                 \
    f(Match, "match")               \
    f(For, "for")                   \
    f(In, "in")                     \
    f(While, "while")               \
    f(Break, "break")               \
    f(Return, "return")             \
    f(Continue, "continue")         \
    f(Func, "func")                 \
    f(Var, "var")                   \
    f(Const, "const")               \
    f(Type, "type")                 \
    f(Native, "native")             \
    f(Struct, "struct")             \
    f(Enum, "enum")                 \
    f(Pub, "pub")                   \
    f(Opaque, "opaque")             \
    f(New,    "new")                \
    f(Delete, "delete")             \
    f(Async,  "async")              \
    f(Await,  "await")              \
    PRIM_TYPE_LIST(f)

#define SPECIAL_TOKEN_LIST(f)                   \
    f(Ident, "identifier")                      \
    f(IntLiteral, "integer literal")            \
    f(FloatLiteral, "floating-point literal")   \
    f(CharLiteral, "character literal")         \
    f(StringLiteral, "string literal")          \
    f(LString, "`(")                            \
    f(RString, ")`")                            \
    f(EoF, "end of file")                       \
    f(Error, "invalid token")

#define TOKEN_LIST(f)                   \
    SYMBOL_LIST(f)                      \
    KEYWORD_LIST(f)                     \
    SPECIAL_TOKEN_LIST(f)

typedef enum {
#define f(name, str) tok##name,
    TOKEN_LIST(f)
#undef f
} TokenTag;

// clang-format on

typedef struct {
    TokenTag tag;
    union {
        uintmax_t iVal;
        double fVal;
        char cVal;
    };
    FileLoc fileLoc;
} Token;

static inline const char *token_tag_to_str(TokenTag tag) {
    switch (tag) {
#define f(name, str) case tok##name: return str;
#define g(name, str) case tok##name: return "'"str"'";
        SYMBOL_LIST(g)
        KEYWORD_LIST(g)
        SPECIAL_TOKEN_LIST(f)
#undef f
        default:
            return NULL;
    }
}
