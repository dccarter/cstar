// credits: https://github.com/madmann91/fu/blob/master/src/fu/lang/token.h
#pragma once

#include "core/log.h"
#include "core/utils.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off

#define SYMBOL_LIST(f)              \
    f(LParen, "(")                  \
    f(RParen, ")")                  \
    f(LBracket, "[")                \
    f(RBracket, "]")                \
    f(LBrace, "{")                  \
    f(RBrace, "}")                  \
    f(At,   "@")                    \
    f(Hash, "#")                    \
    f(LNot, "!")                    \
    f(BNot, "~")                    \
    f(Dot, ".")                     \
    f(DotDot, "..")                 \
    f(Elipsis, "...")               \
    f(Question, "?")                \
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
    f(PlusPlus, "++")               \
    f(MinusMinus, "--")             \
    f(PlusEqual, "+=")              \
    f(MinusEqual, "-=")             \
    f(MultEqual, "*=")              \
    f(DivEqual, "/=")               \
    f(ModEqual, "%=")               \
    f(BAndEqual, "&=")              \
    f(BAndDot,   "&.")              \
    f(BXorEqual, "^=")              \
    f(BOrEqual, "|=")               \
    f(Quote,    "`")                \
    f(Substitutue,     "#{")        \
    f(IndexExpr,       ".[")        \
    f(CallOverride,    "()")        \
    f(IndexOverride,   "[]")        \
    f(IndexAssignOvd,  "[]=")       \
    f(AstMacroAccess,  "#.")        \
    f(Define,          "##")        \
    f(BangColon,       "!:")        \

#define KEYWORD_LIST(f)             \
    f(Virtual, "virtual")           \
    f(Auto, "auto")                 \
    f(True, "true")                 \
    f(False, "false")               \
    f(Null,  "null")                \
    f(If, "if")                     \
    f(Else, "else")                 \
    f(Match, "match")               \
    f(For, "for")                   \
    f(In, "in")                     \
    f(Is, "is")                     \
    f(While, "while")               \
    f(Break, "break")               \
    f(Return, "return")             \
    f(Yield,  "yield")              \
    f(Continue, "continue")         \
    f(Func, "func")                 \
    f(Var, "var")                   \
    f(Const, "const")               \
    f(Type, "type")                 \
    f(Native, "native")             \
    f(Extern, "extern")             \
    f(Exception, "exception")       \
    f(Struct, "struct")             \
    f(Enum, "enum")                 \
    f(Pub, "pub")                   \
    f(Opaque, "opaque")             \
    f(Catch,  "catch")              \
    f(Raise,  "raise")              \
    f(Async,  "async")              \
    f(Launch, "launch")             \
    f(Ptrof,   "ptrof")             \
    f(Await,  "await")              \
    f(Delete, "delete")             \
    f(Discard, "discard")           \
    f(Switch, "switch")             \
    f(Case, "case")                 \
    f(Default, "default")           \
    f(Defer, "defer")               \
    f(Macro, "macro")               \
    f(Void, "void")                 \
    f(String, "string")             \
    f(Range,  "range")              \
    f(Module, "module")             \
    f(Import, "import")             \
    f(Include, "include")           \
    f(CDefine, "cDefine")           \
    f(CInclude,"cInclude")          \
    f(CSources, "cSources")         \
    f(As,      "as")                \
    f(Asm,     "asm")               \
    f(From,    "from")              \
    f(Unsafe,  "unsafe")            \
    f(Interface, "interface")       \
    f(This,      "this")            \
    f(ThisClass,  "This")           \
    f(Super,     "super")           \
    f(Class,     "class")           \
    f(Defined,   "defined")         \
    f(Test,      "test")            \
    f(CBuild,    "__cc")            \
    PRIM_TYPE_LIST(f)

#define SPECIAL_TOKEN_LIST(f)                   \
    f(Ident, "identifier")                      \
    f(IntLiteral, "integer literal")            \
    f(FloatLiteral, "floating-point literal")   \
    f(CharLiteral, "character literal")         \
    f(StringLiteral, "string literal")          \
    f(LString, "`(")                            \
    f(RString, ")`")                            \
    f(LStrFmt, "${")                            \
    f(EoF, "end of file")                       \
    f(Error, "invalid token")

#define TOKEN_LIST(f)                   \
    SYMBOL_LIST(f)                      \
    KEYWORD_LIST(f)                     \
    SPECIAL_TOKEN_LIST(f)

typedef enum {
    tokInvalid,
#define f(name, ...) tok## name,
    TOKEN_LIST(f)
#undef f
    tokAssignEqual = tokAssign
} TokenTag;

// clang-format on

typedef struct {
    TokenTag tag;
    union {
        uintmax_t iVal;
        intmax_t uVal;
        double fVal;
        u32 cVal;
    };
    struct LexerBuffer *buffer;
    FileLoc fileLoc;
} Token;

static inline const char *token_tag_to_str(TokenTag tag)
{
    switch (tag) {
#define f(name, str)                                                           \
    case tok##name:                                                            \
        return str;
#define g(name, str, ...)                                                      \
    case tok##name:                                                            \
        return "'" str "'";
        SYMBOL_LIST(g)
        KEYWORD_LIST(g)
        SPECIAL_TOKEN_LIST(f)
#undef g
#undef f
    default:
        return NULL;
    }
}

bool isPrimitiveType(TokenTag tag);
bool isPrimitiveIntegerType(TokenTag tag);
bool isAssignmentOperator(TokenTag tag);
PrtId tokenToPrimitiveTypeId(TokenTag tag);
bool isKeyword(TokenTag tag);

#ifdef __cplusplus
}
#endif
