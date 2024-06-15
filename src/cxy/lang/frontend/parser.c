/**
 * Credits: https://github.com/madmann91/fu/blob/master/src/fu/lang/parser.c
 */

#include "parser.h"
#include "ast.h"
#include "defines.h"
#include "flag.h"
#include "lexer.h"
#include "strings.h"

#include "driver/cc.h"
#include "driver/driver.h"

#include "core/alloc.h"
#include "core/e4c.h"
#include "core/mempool.h"

#include <stdlib.h>
#include <string.h>

E4C_DEFINE_EXCEPTION(ParserException, "Parsing error", RuntimeException);
E4C_DEFINE_EXCEPTION(ErrorLimitExceeded,
                     "Error limit exceeded",
                     RuntimeException);

static void synchronize(Parser *P);

static void synchronizeUntil(Parser *P, TokenTag tag);

static AstNode *expression(Parser *P, bool allowStructs);

static AstNode *statement(Parser *P, bool exprOnly);

static inline AstNode *statementOrExpression(Parser *P)
{
    return statement(P, true);
}

static inline AstNode *statementOnly(Parser *P) { return statement(P, false); }

static AstNode *parseType(Parser *P);

static AstNode *parseUnionType(Parser *P);

static AstNode *primary(Parser *P, bool allowStructs);

static AstNode *macroExpression(Parser *P, AstNode *callee);

static AstNode *callExpression(Parser *P, AstNode *callee);

static AstNode *parsePath(Parser *P);

static AstNode *variable(
    Parser *P, bool isPublic, bool isExport, bool isExpression, bool woInit);

static AstNode *funcDecl(Parser *P, u64 flags);

static AstNode *aliasDecl(Parser *P, bool isPublic, bool isExtern);

static AstNode *enumDecl(Parser *P, bool isPublic);

static AstNode *classOrStructDecl(Parser *P, bool isPublic, bool isExtern);

static AstNode *attributes(Parser *P);

static AstNode *substitute(Parser *P, bool allowStructs);

static AstNode *block(Parser *P);

static AstNode *comptime(Parser *P, AstNode *(*parser)(Parser *));

static void listAddAstNode(AstNodeList *list, AstNode *node)
{
    if (!list->last)
        list->first = node;
    else
        list->last->next = node;
    list->last = node;
}

static inline const char *getTokenString(Parser *P, const Token *tok, bool trim)
{
    size_t start = tok->fileLoc.begin.byteOffset + trim;
    size_t size = tok->fileLoc.end.byteOffset - trim - start;
    if (P->lexer->fileData[start] == '`' &&
        P->lexer->fileData[start + size - 1] == '`') {
        return makeStringSized(
            P->strPool, &P->lexer->fileData[start + 1], size - 2);
    }
    return makeStringSized(P->strPool, &P->lexer->fileData[start], size);
}

static inline const char *getStringLiteral(Parser *P, const Token *tok)
{
    size_t start = tok->fileLoc.begin.byteOffset + 1;
    size_t size = tok->fileLoc.end.byteOffset - start - 1;
    char *str = mallocOrDie(size + 1), *p = str;
    size = escapeString(&P->lexer->fileData[start], size, str, size);

    const char *s = makeStringSized(P->strPool, str, size);
    free(str);
    return s;
}

static inline Token *current(Parser *parser) { return &parser->ahead[1]; }

static inline Token *previous(Parser *parser) { return &parser->ahead[0]; }

static inline Token *advance(Parser *parser)
{
    if (current(parser)->tag != tokEoF) {
        parser->ahead[0] = parser->ahead[1];
        parser->ahead[1] = parser->ahead[2];
        parser->ahead[2] = parser->ahead[3];
        parser->ahead[3] = advanceLexer(parser->lexer);
    }

    return previous(parser);
}

static inline Token *peek(Parser *parser, u32 index)
{
    csAssert(index <= 2, "len out of bounds");
    return &parser->ahead[1 + index];
}

static Token *parserCheck(Parser *parser, const TokenTag tags[], u32 count)
{
    for (u32 i = 0; i < count; i++) {
        if (current(parser)->tag == tags[i])
            return current(parser);
    }
    return NULL;
}

static Token *parserCheckPeek(Parser *parser,
                              u32 index,
                              const TokenTag tags[],
                              u32 count)
{
    Token *tok = peek(parser, index);
    if (tok == NULL)
        return tok;

    for (u32 i = 0; i < count; i++) {
        if (tok->tag == tags[i])
            return tok;
    }
    return NULL;
}

static Token *parserMatch(Parser *parser, TokenTag tags[], u32 count)
{
    if (parserCheck(parser, tags, count))
        return advance(parser);
    return NULL;
}

// clang-format off
#define check(P, ...) \
({ TokenTag LINE_VAR(tags)[] = { __VA_ARGS__, tokEoF }; parserCheck((P), LINE_VAR(tags), sizeof__(LINE_VAR(tags))-1); })

#define checkPeek(P, I, ...) \
({ TokenTag LINE_VAR(tags)[] = { __VA_ARGS__, tokEoF }; parserCheckPeek((P), (I), LINE_VAR(tags), sizeof__(LINE_VAR(tags))-1); })

#define match(P, ...) \
({ TokenTag LINE_VAR(mtags)[] = { __VA_ARGS__, tokEoF }; parserMatch((P), LINE_VAR(mtags), sizeof__(LINE_VAR(mtags))-1); })

// clang-format on

static bool isEoF(Parser *parser) { return current(parser)->tag == tokEoF; }

static void parserError(Parser *parser,
                        const FileLoc *loc,
                        cstring msg,
                        FormatArg *args)
{
    FileLoc copy = *loc;
    advance(parser);
    logError(parser->L, &copy, msg, args);
    E4C_THROW_CTX(ParserException, "", parser);
}

static void parserWarnThrow(Parser *parser,
                            const FileLoc *loc,
                            cstring msg,
                            FormatArg *args)
{
    advance(parser);
    logWarning(parser->L, loc, msg, args);
    E4C_THROW_CTX(ParserException, "", parser);
}

static Token *consume(Parser *parser, TokenTag id, cstring msg, FormatArg *args)
{
    Token *tok = check(parser, id);
    if (tok == NULL) {
        const Token curr = *current(parser);
        parserError(parser, &curr.fileLoc, msg, args);
    }

    return advance(parser);
}

static Token *consume0(Parser *parser, TokenTag id)
{
    return consume(
        parser,
        id,
        "unexpected token, expecting '{s}', but got '{s}'",
        (FormatArg[]){{.s = token_tag_to_str(id)},
                      {.s = token_tag_to_str(current(parser)->tag)}});
}

static void reportUnexpectedToken(Parser *P, cstring expected)
{
    Token cur = *current(P);
    parserError(
        P,
        &cur.fileLoc,
        "unexpected token '{s}', expecting {s}",
        (FormatArg[]){{.s = token_tag_to_str(cur.tag)}, {.s = expected}});
}

AstNode *newAstNode(Parser *P, const FilePos *start, const AstNode *init)
{
    AstNode *node = allocFromMemPool(P->memPool, sizeof(AstNode));
    memcpy(node, init, sizeof(AstNode));
    node->loc.fileName = P->lexer->fileName;
    node->loc.begin = *start;
    node->loc.end = previous(P)->fileLoc.end;
    return node;
}

static AstNode *parseMany(Parser *P,
                          TokenTag stop,
                          TokenTag sep,
                          AstNode *(with)(Parser *))
{
    AstNodeList list = {NULL};
    while (!check(P, stop) && !isEoF(P)) {
        listAddAstNode(&list, with(P));
        if (!match(P, sep) && !check(P, stop)) {
            parserError(P,
                        &current(P)->fileLoc,
                        "unexpected token '{s}', expecting '{s}' or '{s}'",
                        (FormatArg[]){{.s = token_tag_to_str(current(P)->tag)},
                                      {.s = token_tag_to_str(stop)},
                                      {.s = token_tag_to_str(sep)}});
        }
    }

    return list.first;
}

static AstNode *parseMany2(Parser *P,
                           TokenTag stop1,
                           TokenTag stop2,
                           TokenTag sep,
                           AstNode *(with)(Parser *))
{
    AstNodeList list = {NULL};
    while (!check(P, stop1, stop2) && !isEoF(P)) {
        listAddAstNode(&list, with(P));
        if (!match(P, sep) && !check(P, stop1, stop2)) {
            parserError(P,
                        &current(P)->fileLoc,
                        "unexpected token '{s}', expecting '{s}/{s}' or '{s}'",
                        (FormatArg[]){{.s = token_tag_to_str(current(P)->tag)},
                                      {.s = token_tag_to_str(stop1)},
                                      {.s = token_tag_to_str(stop2)},
                                      {.s = token_tag_to_str(sep)}});
        }
    }

    return list.first;
}

static inline AstNode *parseManyNoSeparator(Parser *P,
                                            TokenTag stop,
                                            AstNode *(with)(Parser *))
{
    AstNodeList list = {NULL};
    while (!check(P, stop) && !isEoF(P)) {
        listAddAstNode(&list, with(P));
    }

    return list.first;
}

static AstNode *parseAtLeastOne(Parser *P,
                                cstring msg,
                                TokenTag stop,
                                TokenTag start,
                                AstNode *(with)(Parser *P))
{
    AstNode *nodes = parseMany(P, stop, start, with);
    if (nodes == NULL) {
        parserError(P,
                    &current(P)->fileLoc,
                    "expecting at least 1 {s}",
                    (FormatArg[]){{.s = msg}});
    }

    return nodes;
}

static inline AstNode *parseNull(Parser *P)
{
    const Token *tok = consume0(P, tokNull);
    return newAstNode(P, &tok->fileLoc.begin, &(AstNode){.tag = astNullLit});
}

static inline AstNode *parseBool(Parser *P)
{
    const Token *tok = match(P, tokTrue, tokFalse);
    if (tok == NULL) {
        reportUnexpectedToken(P, "bool literals i.e 'true'/'false'");
    }
    return newAstNode(P,
                      &tok->fileLoc.begin,
                      &(AstNode){.tag = astBoolLit,
                                 .boolLiteral.value = tok->tag == tokTrue});
}

static inline AstNode *parseChar(Parser *P)
{
    const Token *tok = consume0(P, tokCharLiteral);
    AstNode *node = newAstNode(
        P,
        &tok->fileLoc.begin,
        &(AstNode){.tag = astCharLit, .charLiteral.value = tok->cVal});

    if (match(P, tokColon)) {
        AstNode *type = parseType(P);
        return newAstNode(
            P,
            &tok->fileLoc.begin,
            &(AstNode){.tag = astTypedExpr,
                       .typedExpr = {.expr = node, .type = type}});
    }

    return node;
}

static inline AstNode *parseInteger(Parser *P)
{
    const Token *tok = consume0(P, tokIntLiteral);
    AstNode *type = NULL;
    AstNode *node = newAstNode(
        P,
        &tok->fileLoc.begin,
        &(AstNode){.tag = astIntegerLit, .intLiteral.uValue = tok->iVal});
    if (match(P, tokColon)) {
        type = parseType(P);
        return newAstNode(
            P,
            &tok->fileLoc.begin,
            &(AstNode){.tag = astTypedExpr,
                       .typedExpr = {.expr = node, .type = type}});
    }
    return node;
}

static inline AstNode *parseFloat(Parser *P)
{
    const Token *tok = consume0(P, tokFloatLiteral);
    AstNode *node = newAstNode(
        P,
        &tok->fileLoc.begin,
        &(AstNode){.tag = astFloatLit, .floatLiteral.value = tok->fVal});
    if (match(P, tokColon)) {
        AstNode *type = parseType(P);
        return newAstNode(
            P,
            &tok->fileLoc.begin,
            &(AstNode){.tag = astTypedExpr,
                       .typedExpr = {.expr = node, .type = type}});
    }
    return node;
}

static inline AstNode *parseString(Parser *P)
{
    const Token tok = *consume0(P, tokStringLiteral);
    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astStringLit,
                   .stringLiteral.value = getStringLiteral(P, &tok)});
}

static inline AstNode *parseIdentifier(Parser *P)
{
    const Token tok = *consume0(P, tokIdent);
    AstNode *ident =
        newAstNode(P,
                   &tok.fileLoc.begin,
                   &(AstNode){.tag = astIdentifier,
                              .ident.value = getTokenString(P, &tok, false)});
    if (check(P, tokLNot))
        return macroExpression(P, ident);

    return ident;
}

static inline AstNode *parseIdentifierWithAlias(Parser *P)
{
    Token tok = *consume0(P, tokIdent);
    cstring name = getTokenString(P, &tok, false);

    cstring alias = NULL;
    if (match(P, tokFatArrow)) {
        alias = name;
        name = getTokenString(P, consume0(P, tokIdent), false);
    }

    return newAstNode(P,
                      &tok.fileLoc.begin,
                      &(AstNode){.tag = astIdentifier,
                                 .ident = {.value = name, .alias = alias}});
}

static inline AstNode *primitive(Parser *P)
{
    const Token tok = *current(P);
    if (!isPrimitiveType(tok.tag)) {
        reportUnexpectedToken(P, "a primitive type");
    }
    advance(P);
    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astPrimitiveType,
                   .primitiveType.id = tokenToPrimitiveTypeId(tok.tag)});
}

static inline AstNode *expressionWithoutStructs(Parser *P)
{
    return expression(P, false);
}

static inline AstNode *expressionWithStructs(Parser *P)
{
    return expression(P, true);
}

static AstNode *member(Parser *P, const FilePos *begin, AstNode *operand)
{
    AstNode *member;

    if (check(P, tokIntLiteral))
        member = parseInteger(P);
    else if (check(P, tokSubstitutue))
        member = substitute(P, false);
    else
        member = parsePath(P);

    return newAstNode(
        P,
        begin,
        &(AstNode){.tag = astMemberExpr,
                   .memberExpr = {.target = operand, .member = member}});
}

static AstNode *indexExpr(Parser *P, AstNode *operand)
{
    Token tok = *consume0(P, tokIndexExpr);
    AstNode *index = expressionWithoutStructs(P);
    consume0(P, tokRBracket);

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astIndexExpr,
                   .indexExpr = {.target = operand, .index = index}});
}

static AstNode *postfix(Parser *P, AstNode *(parsePrimary)(Parser *, bool))
{
    AstNode *operand = parsePrimary(P, true);
    while (true) {
        switch (current(P)->tag) {
        case tokPlusPlus:
        case tokMinusMinus:
            break;
        case tokDot: {
            const Token tok = *advance(P);
            bool isBuiltin = match(P, tokHash) != NULL;
            operand = member(P, &tok.fileLoc.begin, operand);
            operand->flags |= (isBuiltin ? flgBuiltin : flgNone);
            continue;
        }
        case tokIndexExpr: {
            operand = indexExpr(P, operand);
            continue;
        }
        case tokLNot:
            operand = macroExpression(P, operand);
            continue;
        case tokLParen:
            operand = callExpression(P, operand);
            continue;
        default:
            return operand;
        }

        const TokenTag tag = advance(P)->tag;
        return newAstNode(
            P,
            &operand->loc.begin,
            &(AstNode){.tag = astUnaryExpr,
                       .unaryExpr = {.operand = operand,
                                     .op = tokenToPostfixUnaryOperator(tag)}});
    }

    unreachable("unreachable");
}

static AstNode *fieldExpr(Parser *P);

static AstNode *structExpr(Parser *P,
                           AstNode *lhs,
                           AstNode *(parseField)(Parser *));

static AstNode *functionParam(Parser *P);

static AstNode *prefix(Parser *P, AstNode *(parsePrimary)(Parser *, bool))
{
    bool isBand = check(P, tokBAnd);
    if (check(P, tokMinus) && peek(P, 1)->tag == tokIntLiteral) {
        consume0(P, tokMinus);
        AstNode *lit = parseInteger(P);
        lit->intLiteral.isNegative = true;
        lit->intLiteral.value = -((i64)lit->intLiteral.uValue);
        return lit;
    }

    if (check(P, tokDot) && peek(P, 1)->tag == tokIdent) {
        const Token tok = *advance(P);
        AstNode *member = parseIdentifier(P);
        return newAstNode(
            P,
            &tok.fileLoc.begin,
            &(AstNode){.tag = astMemberExpr, .memberExpr = {.member = member}});
    }

    if (match(P, tokDefined)) {
        const Token tok = *previous(P);
        cstring name = NULL;
        if (match(P, tokLParen)) {
            name = getTokenString(P, consume0(P, tokIdent), false);
            consume0(P, tokRParen);
        }
        else
            name = getTokenString(P, consume0(P, tokIdent), false);

        return newAstNode(P,
                          &tok.fileLoc.begin,
                          &(AstNode){.tag = astBoolLit,
                                     .boolLiteral.value = preprocessorHasMacro(
                                         &P->cc->preprocessor, name, NULL)});
    }

    switch (current(P)->tag) {
#define f(O, T, ...) case tok##T:
        AST_PREFIX_EXPR_LIST(f)
#undef f
        break;
    default:
        return postfix(P, parsePrimary);
    }

    const Token tok = *advance(P);
    AstNode *operand = prefix(P, parsePrimary);

    if (!isBand) {
        return newAstNode(
            P,
            &tok.fileLoc.begin,
            &(AstNode){.tag = astUnaryExpr,
                       .unaryExpr = {.operand = operand,
                                     .op = tokenToUnaryOperator(tok.tag),
                                     .isPrefix = true}});
    }
    else {
        return newAstNode(
            P,
            &tok.fileLoc.begin,
            &(AstNode){.tag = astAddressOf,
                       .unaryExpr = {.operand = operand, .op = opAddrOf}});
    }
}

static AstNode *assign(Parser *P, AstNode *(parsePrimary)(Parser *, bool));

static AstNode *binary(Parser *P,
                       AstNode *lhs,
                       int prec,
                       AstNode *(parsePrimary)(Parser *, bool))
{
    if (lhs == NULL)
        lhs = prefix(P, parsePrimary);

    while (!isEoF(P)) {
        const Token tok = *current(P);
        if (tok.tag == tokMinus && checkPeek(P, 1, tokFunc))
            break;

        Operator op = tokenToBinaryOperator(tok.tag);
        if (op == opInvalid)
            break;

        int nextPrecedence = getBinaryOpPrecedence(op);
        if (nextPrecedence >= prec)
            break;

        advance(P);
        AstNode *rhs = binary(P, NULL, nextPrecedence, parsePrimary);
        lhs = newAstNode(
            P,
            &lhs->loc.begin,
            &(AstNode){.tag = astBinaryExpr,
                       .binaryExpr = {.lhs = lhs, .op = op, .rhs = rhs}});
    }

    return lhs;
}

static AstNode *assign(Parser *P, AstNode *(parsePrimary)(Parser *, bool))
{
    AstNode *lhs = prefix(P, parsePrimary);
    const Token tok = *current(P);
    if (isAssignmentOperator(tok.tag)) {
        advance(P);
        AstNode *rhs = assign(P, parsePrimary);

        return newAstNode(
            P,
            &lhs->loc.begin,
            &(AstNode){.tag = astAssignExpr,
                       .assignExpr = {.lhs = lhs,
                                      .op = tokenToAssignmentOperator(tok.tag),
                                      .rhs = rhs}});
    }

    return binary(P, lhs, getMaxBinaryOpPrecedence(), parsePrimary);
}

static AstNode *ternary(Parser *P, AstNode *(parsePrimary)(Parser *, bool))
{
    AstNode *cond = assign(P, parsePrimary);
    if (match(P, tokQuestion)) {
        AstNode *lhs = NULL;
        // allow ?: operator
        if (!match(P, tokColon)) {
            lhs = ternary(P, parsePrimary);
            consume0(P, tokColon);
        }
        AstNode *rhs = ternary(P, parsePrimary);

        return newAstNode(
            P,
            &cond->loc.begin,
            &(AstNode){
                .tag = astTernaryExpr,
                .ternaryExpr = {.cond = cond, .body = lhs, .otherwise = rhs}});
    }

    return cond;
}

static AstNode *stringExpr(Parser *P)
{
    const Token tok = *consume0(P, tokLString);
    AstNodeList parts = {NULL};
    while (!check(P, tokRString) && !isEoF(P)) {
        if (check(P, tokLStrFmt)) {
            advance(P);
            continue;
        }
        listAddAstNode(&parts, expression(P, false));
    }
    consume0(P, tokRString);
    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astStringExpr, .stringExpr.parts = parts.first});
}

static inline bool maybeClosure(Parser *P)
{
    return (peek(P, 1)->tag == tokElipsis)     // (...a: Type)
           || (peek(P, 2)->tag == tokColon)    // (a: Type), () : Type
           || (peek(P, 2)->tag == tokFatArrow) // () =>
        ;
}

static AstNode *functionParam(Parser *P)
{
    AstNode *attrs = NULL;
    Token tok = *current(P);
    if (check(P, tokAt))
        attrs = attributes(P);

    u64 flags = match(P, tokElipsis) ? flgVariadic : flgNone;
    const char *name = getTokenString(P, consume0(P, tokIdent), false);
    consume0(P, tokColon);
    bool isConst = match(P, tokConst);
    AstNode *type = parseType(P), *def = NULL;
    type->flags |= (isConst ? flgConst : flgNone);

    if (match(P, tokAssign)) {
        def = expression(P, false);
    }

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astFuncParamDecl,
                   .attrs = attrs,
                   .flags = flags,
                   .funcParam = {.name = name, .type = type, .def = def}});
}

static AstNode *implicitCast(Parser *P)
{
    AstNode *expr;
    Token tok = *consume0(P, tokLess);
    AstNode *to = parseType(P);
    consume0(P, tokGreater);
    expr = expressionWithoutStructs(P);

    return makeAstNode(
        P->memPool,
        &tok.fileLoc,
        &(AstNode){.tag = astCastExpr, .castExpr = {.to = to, .expr = expr}});
}

static AstNode *range(Parser *P)
{
    AstNode *start, *end, *step = NULL;
    bool down = false;
    Token tok = *consume0(P, tokRange);
    consume0(P, tokLParen);
    start = expressionWithoutStructs(P);
    consume0(P, tokComma);
    end = expressionWithoutStructs(P);
    if (match(P, tokComma)) {
        step = expressionWithoutStructs(P);
    }
    if (match(P, tokComma)) {
        consume0(P, tokTrue);
        down = true;
    }
    consume0(P, tokRParen);

    return makeAstNode(P->memPool,
                       &tok.fileLoc,
                       &(AstNode){.tag = astRangeExpr,
                                  .rangeExpr = {.start = start,
                                                .end = end,
                                                .step = step,
                                                .down = down}});
}

static AstNode *closure(Parser *P)
{
    AstNode *ret = NULL, *body = NULL;
    u64 flags = match(P, tokAsync) ? flgAsync : flgNone;
    Token tok = *consume0(P, tokLParen);
    AstNode *params = parseMany(P, tokRParen, tokComma, functionParam);
    consume0(P, tokRParen);

    if (match(P, tokColon)) {
        ret = parseType(P);
    }
    consume0(P, tokFatArrow);

    body = expression(P, true);
    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){
            .tag = astClosureExpr,
            .flags = flags,
            .closureExpr = {.params = params, .ret = ret, .body = body}});
}

static AstNode *tuple(
    Parser *P,
    cstring msg,
    bool strict,
    AstNode *(create)(Parser *, const FilePos *, AstNode *, bool),
    AstNode *(with)(Parser *P))
{
    const Token start = *consume0(P, tokLParen);
    AstNode *args = parseMany(P, tokRParen, tokComma, with);
    consume0(P, tokRParen);

    return create(P, &start.fileLoc.begin, args, strict);
}

static AstNode *parseTupleType(Parser *P)
{
    Token tok = *consume0(P, tokLParen);
    AstNode *elems = parseMany(P, tokRParen, tokComma, parseType);
    consume0(P, tokRParen);
    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astTupleType, .tupleType = {.elements = elems}});
}

static AstNode *parseArrayType(Parser *P)
{
    AstNode *type = NULL, *dim = NULL;
    Token tok = *consume0(P, tokLBracket);
    type = parseType(P);
    if (match(P, tokComma)) {
        dim = expressionWithoutStructs(P);
    }
    consume0(P, tokRBracket);
    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astArrayType,
                   .arrayType = {.elementType = type, .dim = dim}});
}

static AstNode *parseGenericParam(Parser *P)
{
    AstNodeList constraints = {NULL};
    Token tok = *consume0(P, tokIdent);
    if (match(P, tokColon)) {
        do {
            listAddAstNode(&constraints, parsePath(P));
            if (!match(P, tokBOr))
                break;
        } while (!isEoF(P));
    }

    AstNode *defaultValue = NULL;
    if (match(P, tokAssign))
        defaultValue = parsePath(P);

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astGenericParam,
                   .genericParam = {.name = getTokenString(P, &tok, false),
                                    .constraints = constraints.first,
                                    .defaultValue = defaultValue}});
}

// async enclosure(a: T, ...b:T[]) -> T;
static AstNode *parseFuncType(Parser *P)
{
    AstNode *gParams = NULL, *params = NULL, *ret = NULL;
    Token tok = *current(P);
    u64 flags = match(P, tokAsync) ? flgAsync : flgNone;
    consume0(P, tokFunc);
    if (match(P, tokLBracket)) {
        gParams = parseMany(P, tokRBracket, tokComma, parseGenericParam);
        consume0(P, tokRBracket);
    }

    consume0(P, tokLParen);
    params = parseMany(P, tokRParen, tokComma, functionParam);
    consume0(P, tokRParen);

    consume0(P, tokThinArrow);
    ret = parseType(P);

    AstNode *func =
        newAstNode(P,
                   &tok.fileLoc.begin,
                   &(AstNode){.tag = astFuncType,
                              .flags = flags,
                              .funcType = {.params = params, .ret = ret}});
    if (gParams) {
        return newAstNode(
            P,
            &tok.fileLoc.begin,
            &(AstNode){.tag = astGenericDecl,
                       .genericDecl = {.params = gParams, .decl = func}});
    }
    return func;
}

static AstNode *createTupleOrGroupExpression(Parser *P,
                                             const FilePos *begin,
                                             AstNode *node,
                                             bool orGroup)
{

    if (node && node->next == NULL && orGroup)
        return newAstNode(
            P, begin, &(AstNode){.tag = astGroupExpr, .groupExpr.expr = node});

    return newAstNode(
        P, begin, &(AstNode){.tag = astTupleExpr, .tupleExpr.elements = node});
}

static AstNode *parsePointerType(Parser *P)
{
    Token tok = *consume0(P, tokBAnd);
    u64 flags = match(P, tokConst) ? flgConst : flgNone;
    AstNode *pointed = parseType(P);

    return newAstNode(P,
                      &tok.fileLoc.begin,
                      &(AstNode){.tag = astPointerType,
                                 .flags = flags,
                                 .pointerType = {.pointed = pointed}});
}

static AstNode *parenExpr(Parser *P, bool strict)
{
    if (maybeClosure(P))
        return closure(P);

    return tuple(P,
                 "expression",
                 strict,
                 createTupleOrGroupExpression,
                 expressionWithoutStructs);
}

static AstNode *macroExpression(Parser *P, AstNode *callee)
{
    AstNode *args = NULL;
    consume0(P, tokLNot);
    if (check(P, tokLParen)) {
        consume0(P, tokLParen);
        args = parseMany(P, tokRParen, tokComma, expressionWithStructs);
        consume0(P, tokRParen);
    }

    if (check(P, tokLBrace)) {
        AstNode *lastArg = block(P);
        if (args == NULL) {
            args = lastArg;
        }
        else {
            getLastAstNode(args)->next = lastArg;
        }
    }

    return newAstNode(
        P,
        &callee->loc.begin,
        &(AstNode){.tag = astMacroCallExpr,
                   .flags = flgComptime,
                   .macroCallExpr = {.callee = callee, .args = args}});
}

static AstNode *parseCallArguments(Parser *P)
{
    Token tok = *current(P);
    bool isSpread = match(P, tokElipsis);
    AstNode *expr = expressionWithoutStructs(P);
    return isSpread ? newAstNode(P,
                                 &tok.fileLoc.begin,
                                 &(AstNode){.tag = astSpreadExpr,
                                            .spreadExpr.expr = expr})
                    : expr;
}

static AstNode *callExpression(Parser *P, AstNode *callee)
{
    consume0(P, tokLParen);
    AstNode *args = parseMany(P, tokRParen, tokComma, parseCallArguments);
    consume0(P, tokRParen);

    return newAstNode(P,
                      &callee->loc.begin,
                      &(AstNode){.tag = astCallExpr,
                                 .callExpr = {.callee = callee, .args = args}});
}

static AstNode *block(Parser *P)
{
    AstNodeList stmts = {NULL};
    Token tok = *current(P);
    u64 unsafe = match(P, tokUnsafe) != NULL ? flgUnsafe : flgNone;

    consume0(P, tokLBrace);
    while (!check(P, tokRBrace, tokEoF)) {
        E4C_TRY_BLOCK(
            {
                listAddAstNode(&stmts, statement(P, false));
                match(P, tokSemicolon);
            } E4C_CATCH(ParserException) {
                synchronizeUntil(P, tokRBrace);
                break;
            })
    }
    consume0(P, tokRBrace);

    return newAstNode(P,
                      &tok.fileLoc.begin,
                      &(AstNode){.tag = astBlockStmt,
                                 .flags = unsafe,
                                 .blockStmt = {.stmts = stmts.first}});
}

static AstNode *assemblyOutput(Parser *P)
{
    Token tok = *consume0(P, tokStringLiteral);
    cstring constraint = getTokenString(P, &tok, true);
    if (constraint[0] != '=' || constraint[1] == '\0') {
        parserError(
            P,
            &tok.fileLoc,
            "unexpected inline assembly output constraints, expecting "
            "constraint to start with '=' followed by a character, got \"{s}\"",
            (FormatArg[]){{.s = constraint}});
    }

    consume0(P, tokLParen);
    AstNode *expr = expressionWithoutStructs(P);
    consume0(P, tokRParen);

    if (!nodeIsLeftValue(expr)) {
        parserError(P,
                    &expr->loc,
                    "expecting an L-value for inline assembly output",
                    NULL);
    }
    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astAsmOperand,
                   .asmOperand = {.constraint = constraint, .operand = expr}});
}

static AstNode *assemblyInput(Parser *P)
{
    Token tok = *consume0(P, tokStringLiteral);
    cstring constraint = getTokenString(P, &tok, true);
    if (constraint[0] == '\0') {
        parserError(P,
                    &tok.fileLoc,
                    "unexpected inline assembly input constraints, expecting "
                    "a non empty string, got \"{s}\"",
                    (FormatArg[]){{.s = constraint}});
    }

    consume0(P, tokLParen);
    AstNode *expr = expressionWithoutStructs(P);
    consume0(P, tokRParen);

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astAsmOperand,
                   .asmOperand = {.constraint = constraint, .operand = expr}});
}

static AstNode *assembly(Parser *P)
{
    AstNodeList stmts = {NULL};
    Token tok = *consume0(P, tokAsm);

    consume0(P, tokLParen);
    cstring template = getTokenString(P, consume0(P, tokStringLiteral), true);
    AstNode *outputs = NULL, *inputs = NULL, *clobbers = NULL, *flags = NULL;
    if (match(P, tokColon)) {
        outputs = parseMany2(P, tokColon, tokRParen, tokComma, assemblyOutput);
    }
    if (match(P, tokColon)) {
        inputs = parseMany2(P, tokColon, tokRParen, tokComma, assemblyInput);
    }
    if (match(P, tokColon)) {
        clobbers = parseMany2(P, tokColon, tokRParen, tokComma, parseString);
    }
    if (match(P, tokColon)) {
        flags = parseMany2(P, tokColon, tokRParen, tokComma, parseString);
    }
    consume0(P, tokRParen);

    return newAstNode(P,
                      &tok.fileLoc.begin,
                      &(AstNode){.tag = astAsm,
                                 .inlineAssembly = {.text = template,
                                                    .outputs = outputs,
                                                    .inputs = inputs,
                                                    .clobbers = clobbers,
                                                    .flags = flags}});
}

static AstNode *array(Parser *P)
{
    Token tok = *consume0(P, tokLBracket);
    AstNode *elems =
        parseMany(P, tokRBracket, tokComma, expressionWithoutStructs);
    consume0(P, tokRBracket);

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astArrayExpr, .arrayExpr = {.elements = elems}});
}

static AstNode *parseTypeOrIndex(Parser *P)
{
    if (check(P, tokIntLiteral))
        return parseInteger(P);
    return parseType(P);
}

static AstNode *pathElement(Parser *P)
{
    AstNode *args = NULL;
    Token tok = *current(P);
    const char *name = NULL;
    bool isKeyword = false;
    switch (tok.tag) {
    case tokIdent:
        name = getTokenString(P, &tok, false);
        break;
    case tokThis:
        name = S_this;
        isKeyword = true;
        break;
    case tokSuper:
        name = S_super;
        isKeyword = true;
        break;
    case tokThisClass:
        name = S_This;
        isKeyword = true;
        break;
    default:
        reportUnexpectedToken(P,
                              "an identifier or the keywords 'this' / 'super'");
    }
    advance(P);

    if (match(P, tokLBracket)) {
        args = parseMany(P, tokRBracket, tokComma, parseTypeOrIndex);
        consume0(P, tokRBracket);
    }

    return newAstNode(P,
                      &tok.fileLoc.begin,
                      &(AstNode){.tag = astPathElem,
                                 .pathElement = {.name = name,
                                                 .args = args,
                                                 .isKeyword = isKeyword}});
}

static AstNode *parsePath(Parser *P)
{
    AstNodeList parts = {NULL};
    Token tok = *current(P);
    if (check(P, tokIdent) && checkPeek(P, 1, tokLNot)) {
        return parseIdentifier(P);
    }

    do {
        listAddAstNode(&parts, pathElement(P));

        if (!check(P, tokDot) || peek(P, 1)->tag != tokIdent)
            break;
        consume0(P, tokDot);
        if (match(P, tokHash)) {
            listAddAstNode(&parts, pathElement(P));
            parts.last->flags |= flgBuiltin;
            break;
        }
    } while (!isEoF(P));

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astPath, .path = {.elements = parts.first}});
}

static AstNode *fieldExpr(Parser *P)
{
    Token tok = *consume0(P, tokIdent);
    const char *name = getTokenString(P, &tok, false);
    consume0(P, tokColon);

    AstNode *value = expression(P, true);

    return newAstNode(P,
                      &tok.fileLoc.begin,
                      &(AstNode){.tag = astFieldExpr,
                                 .next = NULL,
                                 .fieldExpr = {.name = name, .value = value}});
}

static AstNode *structExpr(Parser *P,
                           AstNode *lhs,
                           AstNode *(parseField)(Parser *))
{
    Token tok = *consume0(P, tokLBrace);
    AstNode *fields = parseMany(P, tokRBrace, tokComma, parseField);
    consume0(P, tokRBrace);

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astStructExpr,
                   .structExpr = {.left = lhs, .fields = fields}});
}

static AstNode *untypedExpr(Parser *P, bool allowStructs)
{
    AstNode *expr = NULL;
    const Token tok = *consume0(P, tokHash);
    if (match(P, tokHash)) {
        logWarning(P->L,
                   &previous(P)->fileLoc,
                   "multiple `#` expression markers not necessary",
                   NULL);
        while (match(P, tokHash))
            ;
    }

    if (check(P, tokIdent)) {
        expr = parsePath(P);
    }
    else
        expr = parseType(P);

    expr->flags |= flgTypeinfo;
    return expr;
}

static AstNode *substitute(Parser *P, bool allowStructs)
{
    AstNode *expr = NULL;
    const Token tok = *consume0(P, tokSubstitutue);
    if (match(P, tokHash, tokSubstitutue)) {
        parserError(P,
                    &previous(P)->fileLoc,
                    "compile time markers `#` or `#{` cannot be used in "
                    "current context",
                    NULL);
    }

    expr = assign(P, primary);

    consume0(P, tokRBrace);

    expr->flags |= flgComptime;
    return expr;
}

static AstNode *primary(Parser *P, bool allowStructs)
{
    switch (current(P)->tag) {
    case tokNull:
        return parseNull(P);
    case tokTrue:
    case tokFalse:
        return parseBool(P);
    case tokCharLiteral:
        return parseChar(P);
    case tokIntLiteral:
        return parseInteger(P);
    case tokFloatLiteral:
        return parseFloat(P);
    case tokStringLiteral:
        return parseString(P);
    case tokLString:
        return stringExpr(P);
    case tokLParen:
        return parenExpr(P, true);
    case tokLBrace:
        return block(P);
    case tokLBracket:
        return array(P);
    case tokHash:
        return untypedExpr(P, allowStructs);
    case tokSubstitutue:
        return substitute(P, allowStructs);
    case tokIdent:
    case tokThis:
    case tokSuper: {
        AstNode *path = parsePath(P);
        if (allowStructs && check(P, tokLBrace))
            return structExpr(P, path, fieldExpr);
        return path;
    }
    case tokLess:
        return implicitCast(P);
    case tokRange:
        return range(P);
    case tokAsync:
        return closure(P);
    default:
        reportUnexpectedToken(P, "a primary expression");
    }

    unreachable("UNREACHABLE");
}

static AstNode *expression(Parser *P, bool allowStructs)
{
    AstNode *attrs = NULL;
    if (check(P, tokAt))
        attrs = attributes(P);

    AstNode *expr = ternary(P, primary);
    expr->attrs = attrs;
    Token *tok = NULL;
    if (!P->inCase && (tok = match(P, tokBangColon))) {
        u64 flags = flgUnsafeCast;
        AstNode *type = parseType(P);
        return newAstNode(
            P,
            &expr->loc.begin,
            &(AstNode){.tag = astTypedExpr,
                       .flags = flags,
                       .typedExpr = {.expr = expr, .type = type}});
    }

    return expr;
}

static AstNode *attribute(Parser *P)
{
    Token tok = *consume0(P, tokIdent);
    const char *name = getTokenString(P, &tok, false);
    AstNodeList args = {NULL};
    bool isKvp = false;
    if (match(P, tokLParen) && !isEoF(P)) {
        isKvp = check(P, tokIdent) && checkPeek(P, 1, tokColon);
        while (!check(P, tokRParen, tokEoF)) {
            AstNode *value = NULL;
            const char *pname = NULL;
            Token start = *peek(P, 0);
            if (isKvp) {
                consume0(P, tokIdent);
                pname = getTokenString(P, &start, false);
                consume0(P, tokColon);
            }

            switch (current(P)->tag) {
            case tokTrue:
            case tokFalse:
                value = parseBool(P);
                break;
            case tokCharLiteral:
                value = parseChar(P);
                break;
            case tokIntLiteral:
                value = parseInteger(P);
                break;
            case tokFloatLiteral:
                value = parseFloat(P);
                break;
            case tokStringLiteral:
                value = parseString(P);
                break;
            default:
                reportUnexpectedToken(P, "string/float/int/char/bool literal");
            }

            if (isKvp) {
                listAddAstNode(
                    &args,
                    newAstNode(P,
                               &start.fileLoc.begin,
                               &(AstNode){.tag = astFieldExpr,
                                          .fieldExpr = {.name = pname,
                                                        .value = value}}));
            }
            else {
                listAddAstNode(&args, value);
            }

            match(P, tokComma);
        }
        consume0(P, tokRParen);
    }

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){
            .tag = astAttr,
            .attr = {.name = name, .args = args.first, .kvpArgs = isKvp}});
}

static AstNode *attributes(Parser *P)
{
    Token tok = *consume0(P, tokAt);
    AstNode *attrs;
    if (match(P, tokLBracket)) {
        attrs =
            parseAtLeastOne(P, "attribute", tokRBracket, tokComma, attribute);

        consume0(P, tokRBracket);
    }
    else {
        attrs = attribute(P);
    }

    return attrs;
}

static AstNode *define(Parser *P)
{
    Token tok = *consume0(P, tokDefine);
    u64 flags = flgNone;
    if (match(P, tokPub))
        flags |= flgPublic;

    AstNode *names, *type = NULL;
    if (match(P, tokLParen)) {
        names = parseMany(P, tokRParen, tokComma, parseIdentifierWithAlias);
        consume0(P, tokRParen);
    }
    else {
        names = parseIdentifier(P);
    }

    consume0(P, tokColon);
    type = parseType(P);
    type->flags |= flgExtern;

    AstNode *container = NULL;
    if (match(P, tokAs))
        container = parseIdentifier(P);

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){
            .tag = astDefine,
            .flags = flags,
            .define = {.names = names, .type = type, .container = container}});
}

static AstNode *parseVarDeclName(Parser *P)
{
    if (check(P, tokSubstitutue)) {
        return substitute(P, false);
    }

    return parseIdentifier(P);
}

static AstNode *parseMultipleVariables(Parser *P)
{
    AstNodeList nodes = {NULL};
    do {
        listAddAstNode(&nodes, parseVarDeclName(P));
    } while (match(P, tokComma));

    return nodes.first;
}

static AstNode *variable(
    Parser *P, bool isPublic, bool isExtern, bool isExpression, bool woInit)
{
    Token tok = *current(P);
    uint64_t flags = isPublic ? flgPublic : flgNone;
    flags |= isExtern ? flgExtern : flgNone;
    flags |= tok.tag == tokConst ? flgConst : flgNone;
    bool isComptime = previous(P)->tag == tokHash ||
                      previous(P)->tag == tokSubstitutue ||
                      previous(P)->tag == tokAstMacroAccess;

    if (!match(P, tokConst, tokVar))
        reportUnexpectedToken(P, "var/const to start variable declaration");

    AstNode *names = NULL, *type = NULL, *init = NULL;
    names = isComptime ? parseIdentifier(P) : parseMultipleVariables(P);

    if (!isExpression && (match(P, tokColon) != NULL))
        type = parseType(P);

    if (!isExtern && !woInit) {
        if (tok.tag == tokConst)
            consume0(P, tokAssign);
        if (tok.tag == tokConst || match(P, tokAssign) || isExpression)
            init = expression(P, true);
    }

    if (!(isExpression || woInit || isExtern)) {
        if (init && init->tag == astClosureExpr)
            match(P, tokSemicolon);
        else
            consume(P,
                    tokSemicolon,
                    "';', semicolon required after non-expression variable "
                    "declaration",
                    NULL);
    }

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astVarDecl,
                   .flags = flags,
                   .varDecl = {.names = names, .type = type, .init = init}});
}

static AstNode *forVariable(Parser *P, bool isComptime)
{
    Token tok = *current(P);
    uint64_t flags = tok.tag == tokConst ? flgConst : flgNone;

    if (isComptime && !check(P, tokConst))
        reportUnexpectedToken(P,
                              "unexpect token, comptime `for` variable can "
                              "only be declared as `const`");

    if (!match(P, tokConst, tokVar))
        reportUnexpectedToken(P, "var/const to start variable declaration");

    AstNode *names = NULL, *type = NULL, *init = NULL;
    names = isComptime
                ? parseIdentifier(P)
                : parseAtLeastOne(
                      P, "variable names", tokColon, tokComma, parseIdentifier);

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astVarDecl,
                   .flags = flags,
                   .varDecl = {.names = names, .type = type, .init = init}});
}

static AstNode *macroDecl(Parser *P, bool isPublic)
{
    AstNode *params = NULL, *body = NULL, *ret = NULL;
    Token tok = *current(P);
    consume0(P, tokMacro);
    cstring name = getTokenString(P, consume0(P, tokIdent), false);

    if (match(P, tokLParen)) {
        params = parseMany(P, tokRParen, tokComma, parseIdentifier);
        consume0(P, tokRParen);
    }

    if (check(P, tokAssign) && peek(P, tokLParen)) {
        consume0(P, tokAssign);
        body = expression(P, true);
    }
    else if (match(P, tokLParen)) {
        body = parseManyNoSeparator(P, tokRParen, statementOnly);
        consume0(P, tokRParen);
    }
    else {
        body = statementOrExpression(P);
    }

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){
            .tag = astMacroDecl,
            .flags = isPublic ? flgPublic : flgNone,
            .macroDecl = {.name = name, .params = params, .body = body}});
}

typedef CxyPair(Operator, cstring) OperatorOverload;

static OperatorOverload operatorOverload(Parser *P)
{
    OperatorOverload op = {.f = opInvalid};
    consume0(P, tokQuote);
    if (match(P, tokLBracket)) {
        consume0(P, tokRBracket);
        if (match(P, tokAssign)) {
            op = (OperatorOverload){.f = opIndexAssignOverload,
                                    .s = S_IndexAssignOverload};
        }
        else {
            op = (OperatorOverload){.f = opIndexOverload, .s = S_IndexOverload};
        }
    }
    else if (match(P, tokLParen)) {
        op = (OperatorOverload){.f = opCallOverload, .s = S_CallOverload};
        consume0(P, tokRParen);
    }
    else if (match(P, tokIdent)) {
        Token ident = *previous(P);
        cstring name = getTokenString(P, &ident, false);
        if (name == S_StringOverload_) {
            op = (OperatorOverload){.f = opStringOverload,
                                    .s = S_StringOverload};
        }
        else if (name == S_InitOverload_) {
            op = (OperatorOverload){.f = opInitOverload, .s = S_InitOverload};
        }
        else if (name == S_DeinitOverload_) {
            op = (OperatorOverload){.f = opDeinitOverload,
                                    .s = S_DeinitOverload};
        }
        else if (name == S_DestructorOverload_) {
            op = (OperatorOverload){.f = opDestructorOverload,
                                    .s = S_DestructorOverload};
        }
        else if (name == S_HashOverload_) {
            op = (OperatorOverload){.f = opHashOverload, .s = S_HashOverload};
        }
        else if (name == S_Deref_) {
            op = (OperatorOverload){.f = opDeref, .s = S_Deref};
        }

        if (op.f == opInvalid) {
            parserError(P,
                        &ident.fileLoc,
                        "unexpected operator overload `{s}`",
                        (FormatArg[]){{.s = name}});
        }
    }
    else {
        switch (current(P)->tag) {
        case tokLNot:
            if (checkPeek(P, 1, tokLNot)) {
                op = (OperatorOverload){.f = opTruthy, .s = S_Truthy};
                advance(P);
            }
            else
                op = (OperatorOverload){.f = opNot, .s = S_Not};
            break;
        case tokAwait:
            op = (OperatorOverload){.f = opAwait, .s = S_Await};
            break;

#define f(O, PP, T, S, N)                                                      \
    case tok##T:                                                               \
        op = (OperatorOverload){.f = op##O, .s = S_##O};                       \
        break;
            AST_BINARY_EXPR_LIST(f);

#undef f
        default:
            reportUnexpectedToken(P, "a binary operator to overload");
        }
        advance(P);
    }
    consume0(P, tokQuote);
    return op;
}

static AstNode *funcDecl(Parser *P, u64 flags)
{
    AstNode *gParams = NULL, *params = NULL, *ret = NULL, *body = NULL;
    Token tok = *current(P);
    bool Extern = (flags & flgExtern) == flgExtern;
    bool Virtual = (flags & flgVirtual) == flgVirtual;
    bool Member = (flags & flgMember) == flgMember;
    flags &= ~flgMember; // Clear member flags, a function might be static
    flags |= match(P, tokAsync) ? flgAsync : flgNone;

    consume0(P, tokFunc);
    cstring name = NULL;
    Operator op = opInvalid;
    if (Member && check(P, tokQuote)) {
        OperatorOverload overload = operatorOverload(P);
        op = overload.f;
        name = overload.s;
    }
    else {
        name = getTokenString(P, consume0(P, tokIdent), false);
    }

    if (match(P, tokLBracket)) {
        if (Extern)
            reportUnexpectedToken(
                P, "a '(', extern functions cannot have generic parameters");
        if (Virtual)
            reportUnexpectedToken(
                P, "a '(', virtual functions cannot have generic parameters");

        gParams = parseAtLeastOne(
            P, "generic params", tokRBracket, tokComma, parseGenericParam);
        consume0(P, tokRBracket);
    }

    consume0(P, tokLParen);
    params = parseMany(P, tokRParen, tokComma, functionParam);
    consume0(P, tokRParen);

    if (match(P, tokColon))
        ret = parseType(P);
    else if (Extern)
        reportUnexpectedToken(P,
                              "colon before function declaration return type");

    if (!Extern) {
        if (match(P, tokFatArrow)) {
            body = expression(P, true);
            match(P, tokSemicolon);
        }
        else if (check(P, tokLBrace)) {
            body = block(P);
        }
        else if (Virtual) {
            if (ret == NULL)
                reportUnexpectedToken(
                    P, "':' before virtual function declaration return type");
            else
                flags |= flgAbstract;
        }
        else {
            reportUnexpectedToken(P, "a function body");
        }
    }
    else {
        Token tmp = *current(P);
        if (match(P, tokFatArrow, tokLBrace)) {
            parserError(P,
                        &tmp.fileLoc,
                        "extern functions cannot be declared with a body",
                        NULL);
        }
        // make semi-colon optional
        match(P, tokSemicolon);
    }

    AstNode *func = newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astFuncDecl,
                   .flags = flags,
                   .funcDecl = {
                       .name = name,
                       .operatorOverload = op,
                       .signature = makeFunctionSignature(
                           P->memPool,
                           &(FunctionSignature){.params = params, .ret = ret}),
                       .body = body}});
    if (gParams) {
        return newAstNode(
            P,
            &tok.fileLoc.begin,
            &(AstNode){.tag = astGenericDecl,
                       .flags = flags,
                       .genericDecl = {.params = gParams, .decl = func}});
    }
    return func;
}

static AstNode *ifStatement(Parser *P)
{
    AstNode *ifElse = NULL, *cond = NULL, *body = NULL;
    Token tok = *consume0(P, tokIf);
    consume0(P, tokLParen);
    if (check(P, tokConst, tokVar)) {
        cond = variable(P, false, false, true, false);
    }
    else {
        cond = expression(P, true);
    }
    consume0(P, tokRParen);

    body = statement(P, false);
    if (match(P, tokElse)) {
        ifElse = statement(P, false);
    }

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){
            .tag = astIfStmt,
            .ifStmt = {.cond = cond, .body = body, .otherwise = ifElse}});
}

static AstNode *forStatement(Parser *P, bool isComptime)
{
    AstNode *body = NULL;

    Token tok = *consume0(P, tokFor);

    consume0(P, tokLParen);
    AstNode *var = forVariable(P, isComptime);
    consume0(P, tokColon);
    AstNode *range = expression(P, true);
    consume0(P, tokRParen);
    if (!match(P, tokSemicolon))
        body = statement(P, false);

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astForStmt,
                   .forStmt = {.var = var, .range = range, .body = body}});
}

static AstNode *whileStatement(Parser *P)
{
    AstNode *body = NULL;
    AstNode *cond = NULL;

    Token tok = *consume0(P, tokWhile);

    consume0(P, tokLParen);
    if (check(P, tokConst, tokVar)) {
        cond = variable(P, false, false, true, false);
    }
    else {
        cond = expression(P, true);
    }
    consume0(P, tokRParen);
    if (!match(P, tokSemicolon))
        body = statement(P, false);

    return newAstNode(P,
                      &tok.fileLoc.begin,
                      &(AstNode){.tag = astWhileStmt,
                                 .whileStmt = {.cond = cond, .body = body}});
}

static AstNode *caseStatement(Parser *P)
{
    u64 flags = flgNone;
    Token tok = *current(P);
    AstNode *match = NULL, *body = NULL;
    if (match(P, tokCase) || previous(P)->tag == tokComma) {
        P->inCase = true;
        match = expression(P, false);
        P->inCase = false;
    }
    else {
        consume(
            P, tokDefault, "expecting a 'default' or a 'case' statement", NULL);
        flags |= flgDefault;
    }

    if (!match(P, tokComma)) {
        consume0(P, tokFatArrow);
        if (!check(P, tokCase, tokRBrace)) {
            body = statement(P, false);
        }
    }

    return newAstNode(P,
                      &tok.fileLoc.begin,
                      &(AstNode){.tag = astCaseStmt,
                                 .flags = flags,
                                 .caseStmt = {.match = match, .body = body}});
}

static AstNode *switchStatement(Parser *P)
{
    AstNodeList cases = {NULL};
    Token tok = *consume0(P, tokSwitch);

    consume0(P, tokLParen);
    AstNode *cond = expression(P, false);
    consume0(P, tokRParen);

    consume0(P, tokLBrace);
    while (!check(P, tokRBrace, tokEoF)) {
        listAddAstNode(&cases, comptime(P, caseStatement));
    }
    consume0(P, tokRBrace);

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astSwitchStmt,
                   .switchStmt = {.cond = cond, .cases = cases.first}});
}

static AstNode *matchCaseStatement(Parser *P)
{
    u64 flags = flgNone;
    Token tok = *current(P);
    AstNode *match = NULL, *body = NULL;
    AstNode *variable = NULL;
    bool isMulti = previous(P)->tag == tokComma;
    if (match(P, tokCase) || isMulti) {
        P->inCase = true;
        match = parseType(P);
        P->inCase = false;
        if (!isMulti && !check(P, tokComma) && match(P, tokAs)) {
            tok = *current(P);
            bool isPointer = match(P, tokBAnd);
            variable = parseIdentifier(P);
            variable =
                newAstNode(P,
                           &tok.fileLoc.begin,
                           &(AstNode){.tag = astVarDecl,
                                      .varDecl = {.name = variable->ident.value,
                                                  .names = variable}});
            variable->flags |= (isPointer ? flgReference : flgNone);
        }
    }
    else {
        consume(P, tokElse, "expecting an 'else' or a 'case' statement", NULL);
        flags |= flgDefault;
    }

    if (!match(P, tokComma)) {
        // either case X { or case =>
        if (!check(P, tokLBrace))
            consume0(P, tokFatArrow);
        if (!check(P, tokCase, tokRBrace)) {
            body = statement(P, false);
        }
    }

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){
            .tag = astCaseStmt,
            .flags = flags,
            .caseStmt = {.match = match, .body = body, .variable = variable}});
}

static AstNode *matchStatement(Parser *P)
{
    AstNodeList cases = {NULL};
    Token tok = *consume0(P, tokMatch);

    consume0(P, tokLParen);
    AstNode *expr = expression(P, false);
    consume0(P, tokRParen);

    consume0(P, tokLBrace);
    while (!check(P, tokRBrace, tokEoF)) {
        listAddAstNode(&cases, comptime(P, matchCaseStatement));
    }
    consume0(P, tokRBrace);

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astMatchStmt,
                   .matchStmt = {.expr = expr, .cases = cases.first}});
}

static AstNode *deferStatement(Parser *P)
{
    AstNode *expr = NULL;
    Token tok = *consume0(P, tokDefer);
    bool isBlock = check(P, tokLBrace) != NULL;
    expr = expression(P, true);
    if (!isBlock)
        match(P, tokSemicolon);

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astDeferStmt, .deferStmt = {.expr = expr}});
}

static AstNode *returnStatement(Parser *P)
{
    AstNode *expr = NULL;
    Token tok = *consume0(P, tokReturn);
    if (!check(P, tokSemicolon)) {
        expr = expression(P, true);
    }
    match(P, tokSemicolon);

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astReturnStmt, .returnStmt = {.expr = expr}});
}

static AstNode *continueStatement(Parser *P)
{
    Token tok = *current(P);
    if (!match(P, tokBreak, tokContinue)) {
        reportUnexpectedToken(P, "continue/break");
    }

    match(P, tokSemicolon);
    return newAstNode(P,
                      &tok.fileLoc.begin,
                      &(AstNode){.tag = tok.tag == tokContinue ? astContinueStmt
                                                               : astBreakStmt});
}

static AstNode *statement(Parser *P, bool exprOnly)
{
    AstNode *attrs = NULL;
    u64 flags = flgNone;
    if (check(P, tokAt))
        attrs = attributes(P);

    bool isComptime = match(P, tokHash) != NULL;
    if (isComptime && !check(P, tokIf, tokFor, tokWhile, tokSwitch, tokConst)) {
        parserError(P,
                    &current(P)->fileLoc,
                    "current token is not a valid compile time token",
                    NULL);
    }

    AstNode *stmt = NULL;

    switch (current(P)->tag) {
    case tokIf:
        stmt = ifStatement(P);
        break;
    case tokFor:
        stmt = forStatement(P, isComptime);
        break;
    case tokSwitch:
        stmt = switchStatement(P);
        break;
    case tokMatch:
        stmt = matchStatement(P);
        break;
    case tokWhile:
        stmt = whileStatement(P);
        break;
    case tokDefer:
        stmt = deferStatement(P);
        break;
    case tokReturn:
        stmt = returnStatement(P);
        break;
    case tokBreak:
    case tokContinue:
        stmt = continueStatement(P);
        break;
    case tokVar:
    case tokConst:
        stmt = variable(P, false, false, false, false);
        break;
    case tokFunc:
        stmt = funcDecl(P, flgNone);
        break;
    case tokLBrace:
        stmt = block(P);
        break;
    case tokAsm:
        stmt = assembly(P);
        break;
    default: {
        AstNode *expr = expression(P, false);
        stmt = exprOnly ? expr
                        : newAstNode(P,
                                     &expr->loc.begin,
                                     &(AstNode){.tag = astExprStmt,
                                                .exprStmt = {.expr = expr}});
        match(P, tokSemicolon);
        break;
    }
    }

    stmt->attrs = attrs;
    stmt->flags |= (isComptime ? flgComptime : flgNone) | flags;
    return stmt;
}

static AstNode *parseTypeImpl(Parser *P)
{
    AstNode *type;
    Token tok = *current(P);
    if (isPrimitiveType(tok.tag)) {
        type = primitive(P);
    }
    else {
        switch (tok.tag) {
        case tokIdent:
        case tokThisClass:
            type = parsePath(P);
            type->path.isType = true;
            break;
        case tokLParen:
            type = parseTupleType(P);
            break;
        case tokLBracket:
            type = parseArrayType(P);
            break;
        case tokAsync:
        case tokFunc:
            type = parseFuncType(P);
            break;
        case tokBAnd:
            type = parsePointerType(P);
            break;
        case tokVoid:
            advance(P);
            type = makeAstNode(
                P->memPool, &tok.fileLoc, &(AstNode){.tag = astVoidType});
            break;
        case tokString:
            advance(P);
            type = makeAstNode(
                P->memPool, &tok.fileLoc, &(AstNode){.tag = astStringType});
            break;
        case tokCChar:
            advance(P);
            type = makeAstNode(P->memPool,
                               &tok.fileLoc,
                               &(AstNode){.tag = astPrimitiveType,
                                          .primitiveType.id = prtCChar});
            break;
        case tokSubstitutue:
            type = substitute(P, false);
            break;
        case tokAuto:
            advance(P);
            type = makeAstNode(
                P->memPool, &tok.fileLoc, &(AstNode){.tag = astAutoType});
            break;
        default:
            reportUnexpectedToken(P, "a type");
            unreachable("");
        }
    }

    if (match(P, tokQuestion)) {
        type = makeAstNode(
            P->memPool,
            &tok.fileLoc,
            &(AstNode){.tag = astOptionalType, .optionalType.type = type});
    }

    type->flags |= flgTypeAst;
    return type;
}

static AstNode *parseType(Parser *P)
{
    AstNodeList members = {0};
    Token tok = *current(P);
    do {
        listAddAstNode(&members, parseTypeImpl(P));
    } while (match(P, tokBOr));

    if (members.first != members.last) {
        return newAstNode(P,
                          &tok.fileLoc.begin,
                          &(AstNode){.tag = astUnionDecl,
                                     .flags = flgNone,
                                     .unionDecl = {.members = members.first}});
    }
    else {
        return members.first;
    }
}

static AstNode *parseStructField(Parser *P, bool isPrivate)
{
    AstNode *type = NULL, *value = NULL;
    Token tok = *consume0(P, tokIdent);
    cstring name = getTokenString(P, &tok, false);
    if (match(P, tokColon)) {
        type = parseType(P);
    }

    if (type == NULL || check(P, tokAssign)) {
        consume0(P, tokAssign);
        value = expression(P, false);
    }
    // consume optional semicolon
    match(P, tokSemicolon);

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){
            .tag = astFieldDecl,
            .flags = isPrivate ? flgPrivate : flgNone,
            .structField = {.name = name, .type = type, .value = value}});
}

static AstNode *parseClassOrStructMember(Parser *P)
{
    AstNode *member = NULL, *attrs = NULL;
    Token tok = *current(P);

    if (check(P, tokAt))
        attrs = attributes(P);

    bool isPrivate = match(P, tokMinus);
    bool isVirtual = match(P, tokVirtual);
    bool isConst = match(P, tokConst);

    switch (current(P)->tag) {
    case tokIdent:
        member = parseStructField(P, isPrivate);
        break;
    case tokFunc:
    case tokAsync: {
        u64 flags = isVirtual ? flgVirtual : flgNone;
        flags |= isPrivate ? flgNone : flgPublic;
        member = funcDecl(P, flags | flgMember);
        break;
    }
    case tokMacro:
        if (attrs)
            parserError(P,
                        &tok.fileLoc,
                        "attributes cannot be attached to macro declarations",
                        NULL);
        member = macroDecl(P, !isPrivate);
        break;
    case tokType:
        member = aliasDecl(P, !isPrivate, false);
        break;
    case tokStruct:
        member = classOrStructDecl(P, !isPrivate, false);
        break;
    default:
        reportUnexpectedToken(P, "struct member");
    }
    member->flags |= (isConst ? flgConst : flgNone);
    member->attrs = attrs;
    return member;
}

static AstNode *parseInterfaceMember(Parser *P)
{
    AstNode *member = NULL, *attrs = NULL;
    Token tok = *current(P);

    if (check(P, tokAt))
        attrs = attributes(P);

    bool isPrivate = match(P, tokMinus);
    bool isConst = match(P, tokConst);

    switch (current(P)->tag) {
    case tokFunc:
    case tokAsync:
        member = funcDecl(P, isPrivate ? flgExtern : flgPublic | flgExtern);
        break;
    default:
        reportUnexpectedToken(P, "interface member");
    }
    member->flags |= (isConst ? flgConst : flgNone);
    member->attrs = attrs;
    return member;
}

static AstNode *parseComptimeIf(Parser *P, AstNode *(*parser)(Parser *))
{
    AstNode *cond;
    Token tok = *consume0(P, tokIf);
    consume0(P, tokLParen);
    if (check(P, tokConst, tokVar)) {
        cond = variable(P, false, false, true, false);
    }
    else {
        cond = expression(P, true);
    }
    consume0(P, tokRParen);
    consume0(P, tokLBrace);
    AstNode *body = parseManyNoSeparator(P, tokRBrace, parser);
    consume0(P, tokRBrace);

    AstNode *otherwise = NULL;
    if (match(P, tokElse)) {
        if (match(P, tokLBrace)) {
            otherwise = parseManyNoSeparator(P, tokRBrace, parser);
            consume0(P, tokRBrace);
        }
        else {
            consume0(P, tokHash);
            otherwise = parseComptimeIf(P, parser);
        }
    }

    return makeAstNode(
        P->memPool,
        &tok.fileLoc,
        &(AstNode){
            .tag = astIfStmt,
            .flags = flgComptime,
            .ifStmt = {.cond = cond, .body = body, .otherwise = otherwise}});
}

static AstNode *parseComptimeWhile(Parser *P, AstNode *(*parser)(Parser *))
{
    AstNode *cond;
    Token tok = *consume0(P, tokWhile);
    consume0(P, tokLParen);
    if (check(P, tokConst, tokVar)) {
        cond = variable(P, false, false, true, false);
    }
    else {
        cond = expression(P, true);
    }
    consume0(P, tokRParen);
    consume0(P, tokLBrace);
    AstNode *body = parseManyNoSeparator(P, tokRBrace, parser);
    consume0(P, tokRBrace);

    return makeAstNode(P->memPool,
                       &tok.fileLoc,
                       &(AstNode){.tag = astWhileStmt,
                                  .flags = flgComptime,
                                  .whileStmt = {.cond = cond, .body = body}});
}

static AstNode *parseComptimeFor(Parser *P, AstNode *(*parser)(Parser *))
{
    Token tok = *consume0(P, tokFor);
    consume0(P, tokLParen);
    AstNode *var = variable(P, false, false, true, true);
    consume0(P, tokColon);
    AstNode *range = expression(P, true);
    consume0(P, tokRParen);

    consume0(P, tokLBrace);
    AstNode *body = parseManyNoSeparator(P, tokRBrace, parser);
    consume0(P, tokRBrace);

    return makeAstNode(
        P->memPool,
        &tok.fileLoc,
        &(AstNode){.tag = astForStmt,
                   .flags = flgComptime,
                   .forStmt = {.var = var, .range = range, .body = body}});
}

static AstNode *parseComptimeVarDecl(Parser *P, AstNode *(*parser)(Parser *))
{
    AstNode *node = variable(P, false, false, true, false);
    node->flags |= flgComptime;
    return node;
}

static AstNode *comptime(Parser *P, AstNode *(*parser)(Parser *))
{
    if (!match(P, tokHash)) {
        return parser(P);
    }

    switch (current(P)->tag) {
    case tokIf:
        return parseComptimeIf(P, parser);
    case tokWhile:
        return parseComptimeWhile(P, parser);
    case tokFor:
        return parseComptimeFor(P, parser);
    case tokConst:
        return parseComptimeVarDecl(P, parser);
    default:
        parserError(P,
                    &current(P)->fileLoc,
                    "current token is not a valid comptime statement",
                    NULL);
    }
    unreachable("");
}

static AstNode *classOrStructDecl(Parser *P, bool isPublic, bool isExtern)
{
    AstNode *base = NULL, *gParams = NULL, *implements = NULL;
    AstNodeList members = {NULL};
    Token tok = *match(P, tokClass, tokStruct);
    cstring name = getTokenString(P, consume0(P, tokIdent), false);

    if (!isExtern) {
        if (match(P, tokLBracket)) {
            gParams = parseAtLeastOne(P,
                                      "generic type params",
                                      tokRBracket,
                                      tokComma,
                                      parseGenericParam);
            consume0(P, tokRBracket);
        }

        if (tok.tag == tokClass && match(P, tokColon)) {
            // Only classes support inheritance
            if (!check(P, tokColon))
                base = parseType(P);
            if (match(P, tokColon))
                implements = parseAtLeastOne(P,
                                             "interface to implement",
                                             tokLBrace,
                                             tokComma,
                                             parseType);
        }
    }
    else {
        implements = NULL;
    }

    if (!isExtern || check(P, tokLBrace)) {
        consume0(P, tokLBrace);
        while (!check(P, tokRBrace, tokEoF)) {
            listAddAstNode(&members, comptime(P, parseClassOrStructMember));
        }
        consume0(P, tokRBrace);
    }

    AstNode *node = newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = tok.tag == tokClass ? astClassDecl : astStructDecl,
                   .flags = ((isPublic ? flgPublic : flgNone) |
                             (isExtern ? flgExtern : flgNone)),
                   .classDecl = {.name = name,
                                 .members = members.first,
                                 .implements = implements,
                                 .base = base}});

    if (gParams) {
        return newAstNode(
            P,
            &tok.fileLoc.begin,
            &(AstNode){.tag = astGenericDecl,
                       .flags = isPublic ? flgPublic : flgNone,
                       .genericDecl = {.params = gParams, .decl = node}});
    }
    return node;
}

static AstNode *interfaceDecl(Parser *P, bool isPublic)
{
    AstNode *gParams = NULL;
    AstNodeList members = {NULL};
    Token tok = *consume0(P, tokInterface);
    cstring name = getTokenString(P, consume0(P, tokIdent), false);

    if (match(P, tokLBracket)) {
        gParams = parseAtLeastOne(
            P, "generic type params", tokRBracket, tokComma, parseGenericParam);
        consume0(P, tokRBracket);
    }

    consume0(P, tokLBrace);
    while (!check(P, tokRBrace, tokEoF)) {
        listAddAstNode(&members, comptime(P, parseInterfaceMember));
    }
    consume0(P, tokRBrace);

    AstNode *node = newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astInterfaceDecl,
                   .flags = isPublic ? flgPublic : flgNone,
                   .interfaceDecl = {.name = name, .members = members.first}});

    if (gParams) {
        return newAstNode(
            P,
            &tok.fileLoc.begin,
            &(AstNode){.tag = astGenericDecl,
                       .flags = isPublic ? flgPublic : flgNone,
                       .genericDecl = {.params = gParams, .decl = node}});
    }
    return node;
}

static AstNode *enumOption(Parser *P)
{
    AstNode *value = NULL;
    Token tok = *consume0(P, tokIdent);
    cstring name = getTokenString(P, &tok, false);
    if (match(P, tokAssign)) {
        value = expression(P, false);
    }
    return newAstNode(P,
                      &tok.fileLoc.begin,
                      &(AstNode){.tag = astEnumOptionDecl,
                                 .enumOption = {.name = name, .value = value}});
}

static AstNode *enumDecl(Parser *P, bool isPublic)
{
    AstNode *base = NULL, *options = NULL;
    Token tok = *consume0(P, tokEnum);
    cstring name = getTokenString(P, consume0(P, tokIdent), false);

    if (match(P, tokColon)) {
        base = parseType(P);
    }
    consume0(P, tokLBrace);
    options =
        parseAtLeastOne(P, "enum options", tokRBrace, tokComma, enumOption);
    consume0(P, tokRBrace);

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){
            .tag = astEnumDecl,
            .flags = isPublic ? flgPublic : flgNone,
            .enumDecl = {.options = options, .name = name, .base = base}});
}

static AstNode *aliasDecl(Parser *P, bool isPublic, bool isExtern)
{
    AstNode *alias = NULL;
    Token tok = *consume0(P, tokType);
    u64 flags = isPublic ? flgPublic : flgNone;
    flags |= isExtern ? flgExtern : flgNone;
    cstring name = getTokenString(P, consume0(P, tokIdent), false);
    if (!isExtern) {
        if (match(P, tokAssign))
            alias = parseType(P);
        else {
            flags |= flgForwardDecl;
            alias = NULL;
        }
    }
    match(P, tokSemicolon);

    return newAstNode(P,
                      &tok.fileLoc.begin,
                      &(AstNode){.tag = astTypeDecl,
                                 .flags = flags,
                                 .typeDecl = {.name = name, .aliased = alias}});
}

static AstNode *parseCCode(Parser *P)
{
    Token tok = *previous(P);
    if (!match(P, tokCDefine, tokCInclude, tokCSources)) {
        parserError(P,
                    &tok.fileLoc,
                    "unexpected attribute, expecting either `@cDefine` or "
                    "`@cInclude`",
                    NULL);
    }
    CCodeKind kind = getCCodeKind(previous(P)->tag);
    consume0(P, tokLParen);
    AstNode *code = NULL;
    if (kind == cSources) {
        code = parseMany(P, tokRParen, tokComma, parseString);
    }
    else {
        code = parseString(P);
    }
    consume0(P, tokRParen);

    return makeAstNode(
        P->memPool,
        &tok.fileLoc,
        &(AstNode){.tag = astCCode, .cCode = {.what = code, .kind = kind}});
}

static AstNode *declaration(Parser *P)
{
    Token tok = *current(P);
    AstNode *attrs = NULL, *decl = NULL;
    if (check(P, tokAt) && peek(P, 1)->tag == tokCDefine) {
        advance(P);
        return parseCCode(P);
    }

    if (check(P, tokAt))
        attrs = attributes(P);
    bool isPublic = match(P, tokPub) != NULL;
    bool isExtern = false;
    if (check(P, tokExtern)) {
        // do we need to consume native
        switch (peek(P, 1)->tag) {
        case tokType:
        case tokVar:
        case tokConst:
        case tokFunc:
        case tokStruct:
            advance(P);
            isExtern = true;
            break;
        case tokAsync:
            parserError(P,
                        &current(P)->fileLoc,
                        "an async function cannot be marked as extern",
                        NULL);
        default:
            break;
        }
    }

    switch (current(P)->tag) {
    case tokStruct:
    case tokClass:
        decl = classOrStructDecl(P, isPublic, isExtern);
        break;
    case tokInterface:
        decl = interfaceDecl(P, isPublic);
        break;
    case tokEnum:
        decl = enumDecl(P, isPublic);
        break;
    case tokType:
        decl = aliasDecl(P, isPublic, isExtern);
        break;
    case tokVar:
    case tokConst:
        decl = variable(P, isPublic, isExtern, false, false);
        break;
    case tokFunc:
    case tokAsync:
        decl = funcDecl(P,
                        (isPublic ? flgPublic : flgNone) |
                            (isExtern ? flgExtern : flgNone));
        break;
    case tokDefine:
        decl = define(P);
        break;
    case tokMacro:
        if (attrs)
            parserError(P,
                        &tok.fileLoc,
                        "attributes cannot be attached to macro declarations",
                        NULL);
        decl = macroDecl(P, isPublic);
        break;
    case tokExtern:
        parserError(P,
                    &current(P)->fileLoc,
                    "extern can only be used on top level struct, function or "
                    "variable declarations",
                    NULL);
        break;
    default:
        reportUnexpectedToken(P, "a declaration");
    }

#undef isExtern

    decl->flags |= flgTopLevelDecl;
    decl->attrs = attrs;
    return decl;
}

static void synchronize(Parser *P)
{
    // skip current problematic token
    advance(P);
    while (!match(P, tokSemicolon, tokEoF)) {
        switch (current(P)->tag) {
        case tokType:
        case tokStruct:
        case tokClass:
        case tokEnum:
        case tokVar:
        case tokConst:
        case tokAsync:
        case tokFunc:
        case tokAt:
        case tokDefine:
        case tokCDefine:
        case tokInterface:
        case tokPub:
        case tokMacro:
        case tokIf:
        case tokFor:
        case tokSwitch:
        case tokWhile:
        case tokMatch:
            return;
        default:
            advance(P);
        }
    }
}

static AstNode *parseImportEntity(Parser *P)
{
    Token tok = *consume0(P, tokIdent);
    cstring name = getTokenString(P, &tok, false), alias;
    if (match(P, tokAs)) {
        Token *aliasTok = consume0(P, tokIdent);
        alias = getTokenString(P, aliasTok, false);
    }
    else {
        alias = name;
    }

    return makeAstNode(
        P->memPool,
        &tok.fileLoc,
        &(AstNode){.tag = astImportEntity,
                   .importEntity = {.alias = alias, .name = name}});
}

static AstNode *parseModuleDecl(Parser *P)
{
    Token tok = *consume0(P, tokModule);
    Token name = *consume0(P, tokIdent);

    return makeAstNode(
        P->memPool,
        &tok.fileLoc,
        &(AstNode){.tag = astModuleDecl,
                   .moduleDecl = {.name = getTokenString(P, &name, false)}});
}

static AstNode *parseImportDecl(Parser *P)
{
    Token tok = *consume0(P, tokImport);
    AstNode *module;
    AstNode *entities = NULL, *alias = NULL;
    const Type *exports;
    if (check(P, tokIdent)) {
        entities = parseImportEntity(P);
    }
    else if (match(P, tokLBrace)) {
        entities = parseAtLeastOne(
            P, "exported declaration", tokRBrace, tokComma, parseImportEntity);
        consume0(P, tokRBrace);
    }

    if (entities)
        consume0(P, tokFrom);

    module = parseString(P);

    if (entities == NULL && match(P, tokAs))
        alias = parseIdentifier(P);

    exports = compileModule(P->cc, module, entities, alias);
    if (exports == NULL) {
        logWarning(P->L,
                   &tok.fileLoc,
                   "importing module {s} failed",
                   (FormatArg[]){{.s = module->stringLiteral.value}});
        return makeAstNode(
            P->memPool, &tok.fileLoc, &(AstNode){.tag = astError});
    }

    return makeAstNode(P->memPool,
                       &tok.fileLoc,
                       &(AstNode){.tag = astImportDecl,
                                  .type = exports,
                                  .flags = exports->flags,
                                  .import = {.module = module,
                                             .alias = alias,
                                             .entities = entities}});
}

static AstNode *parseImportsDecl(Parser *P)
{
    AstNode *imports = parseImportDecl(P), *next = imports;
    while (check(P, tokImport)) {
        next->next = parseImportDecl(P);
        next = next->next;
    }

    return imports;
}

static AstNode *parseTopLevelDecl(Parser *P)
{
    if (check(P, tokImport))
        return parseImportDecl(P);
    else if (check(P, tokCDefine, tokCInclude, tokCSources)) {
        return parseCCode(P);
    }

    Token tok = *consume0(P, tokCBuild);
    Token srcToken = *consume0(P, tokStringLiteral);
    cstring src = getTokenString(P, &srcToken, true);
    addNativeSourceFile(P->cc, tok.fileLoc.fileName, src);
    return NULL;
}

static void synchronizeUntil(Parser *P, TokenTag tag)
{
    while (!check(P, tag, tokEoF))
        advance(P);
}

Parser makeParser(Lexer *lexer, CompilerDriver *cc)
{
    Parser parser = {.cc = cc,
                     .lexer = lexer,
                     .L = lexer->log,
                     .memPool = cc->pool,
                     .strPool = cc->strings};
    parser.ahead[0] = (Token){.tag = tokEoF};
    for (u32 i = 1; i < TOKEN_BUFFER; i++)
        parser.ahead[i] = advanceLexer(lexer);

    return parser;
}

AstNode *parseProgram(Parser *P)
{
    Token tok = *current(P);

    AstNodeList decls = {NULL};
    AstNode *module = NULL;
    AstNodeList topLevel = {NULL};

    if (check(P, tokModule))
        module = parseModuleDecl(P);

    while (check(P, tokImport) ||
           (check(P, tokAt) &&
            checkPeek(P, 1, tokCDefine, tokCInclude, tokCSources, tokCBuild) &&
            match(P, tokAt))) {
        E4C_TRY_BLOCK(
            {
                AstNode *node = parseTopLevelDecl(P);
                if (node != NULL)
                    listAddAstNode(&topLevel, node);
            } E4C_CATCH(ParserException) { synchronize(E4C_EXCEPTION.ctx); })
    }

    while (!isEoF(P)) {
        E4C_TRY_BLOCK(
            {
                listAddAstNode(&decls, comptime(P, declaration));
            } E4C_CATCH(ParserException) { synchronize(E4C_EXCEPTION.ctx); })
    }

    return newAstNode(P,
                      &tok.fileLoc.begin,
                      &(AstNode){.tag = astProgram,
                                 .program = {.module = module,
                                             .top = topLevel.first,
                                             .decls = decls.first}});
}

AstNode *parseExpression(Parser *P) { return expression(P, false); }
