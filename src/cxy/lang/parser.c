/**
 * Credits: https://github.com/madmann91/fu/blob/master/src/fu/lang/parser.c
 */

#include "lang/parser.h"

#include "core/alloc.h"
#include "core/e4c.h"
#include "core/mempool.h"

#include "lang/ast.h"
#include "lang/lexer.h"

#include <stdlib.h>
#include <string.h>

E4C_DEFINE_EXCEPTION(ParserException, "Parsing error", RuntimeException);
E4C_DEFINE_EXCEPTION(ErrorLimitExceeded,
                     "Error limit exceeded",
                     RuntimeException);

typedef struct {
    AstNode *first;
    AstNode *last;
} AstNodeList;

static void synchronize(Parser *P);
static void synchronizeUntil(Parser *P, TokenTag tag);
static AstNode *expression(Parser *P, bool allowStructs);
static AstNode *statement(Parser *P);
static AstNode *primary(Parser *P, bool allowStructs);
static AstNode *macroExpression(Parser *P, AstNode *callee);
static AstNode *callExpression(Parser *P, AstNode *callee);
static AstNode *parsePath(Parser *P);

static void listAddAstNode(AstNodeList *list, AstNode *node)
{
    if (!list->last)
        list->first = node;
    else
        list->last->next = node;
    list->last = node;
}

static inline const char *getTokenString(Parser *P, const Token *tok)
{
    size_t start = tok->fileLoc.begin.byteOffset;
    size_t size = tok->fileLoc.end.byteOffset - start;
    char *name = allocFromMemPool(P->memPool, size + 1);
    memcpy(name, &P->lexer->fileData[start], size);
    name[size] = 0;
    return name;
}

static inline Token *current(Parser *parser) { return &parser->ahead[1]; }

static inline Token *previous(Parser *parser) { return &parser->ahead[0]; }

static inline Token *advance(Parser *parser)
{
    if (current(parser)->tag != tokEoF) {
        parser->ahead[0] = parser->ahead[1];
        parser->ahead[1] = parser->ahead[2];
        parser->ahead[2] = parser->ahead[3];
        parser->ahead[2] = advanceLexer(parser->lexer);
    }

    return previous(parser);
}

static inline Token *peek(Parser *parser, u32 index)
{
    csAssert(index <= 2, "index out of bounds");
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

static Token *parserMatch(Parser *parser, TokenTag tags[], u32 count)
{
    if (parserCheck(parser, tags, count))
        return advance(parser);
    return NULL;
}

// clang-format off
#define check(P, ...) \
({ TokenTag LineVAR(tags)[] = { __VA_ARGS__, tokEoF }; parserCheck((P), LineVAR(tags), sizeof__(LineVAR(tags))-1); })

#define match(P, ...) \
({ TokenTag LineVAR(mtags)[] = { __VA_ARGS__, tokEoF }; parserCheck((P), LineVAR(mtags), sizeof__(LineVAR(mtags))-1); })

// clang-format on

static bool isEoF(Parser *parser) { return current(parser)->tag == tokEoF; }

static void parserError(Parser *parser,
                        const FileLoc *loc,
                        cstring msg,
                        FormatArg *args)
{
    advance(parser);
    logError(parser->L, loc, msg, args);
    E4C_THROW(ParserException, "");
}

static Token *consume(Parser *parser, TokenTag id, cstring msg, FormatArg *args)
{
    Token *tok = check(parser, id);
    if (tok == NULL) {
        const Token *curr = peek(parser, 0);
        parserError(parser, &curr->fileLoc, msg, args);
    }

    advance(parser);
    return tok;
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
    const Token *cur = current(P);
    parserError(
        P,
        &cur->fileLoc,
        "unexpected token '{s}', expecting {s}",
        (FormatArg[]){{.s = token_tag_to_str(cur->tag)}, {.s = expected}});
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
    return newAstNode(
        P,
        &tok->fileLoc.begin,
        &(AstNode){.tag = astCharLit, .charLiteral.value = 0 /*TODO*/});
}

static inline AstNode *parseInteger(Parser *P)
{
    const Token *tok = consume0(P, tokIntLiteral);
    return newAstNode(
        P,
        &tok->fileLoc.begin,
        &(AstNode){.tag = astIntegerLit, .intLiteral.value = tok->iVal});
}

static inline AstNode *parseFloat(Parser *P)
{
    const Token *tok = consume0(P, tokFloatLiteral);
    return newAstNode(
        P,
        &tok->fileLoc.begin,
        &(AstNode){.tag = astIntegerLit, .floatLiteral.value = tok->fVal});
}

static inline AstNode *parseString(Parser *P)
{
    const Token tok = *consume0(P, tokStringLiteral);
    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astStringLit,
                   .stringLiteral.value = getTokenString(P, &tok)});
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

static AstNode *member(Parser *P, const FilePos *begin, AstNode *operand)
{
    AstNode *member;

    if (check(P, tokIntLiteral))
        member = parseInteger(P);
    else
        member = parsePath(P);

    return newAstNode(
        P,
        begin,
        &(AstNode){.tag = astMemberExpr,
                   .memberExpr = {.target = operand, .member = member}});
}

static AstNode *postfix(Parser *P, AstNode *(parsePrimary)(Parser *, bool))
{
    AstNode *operand = parsePrimary(P, true);
    while (!isEoF(P)) {
        switch (current(P)->tag) {
        case tokPlusPlus:
        case tokMinusMinus:
            break;
        case tokDot: {
            const Token tok = *advance(P);
            operand = member(P, &tok.fileLoc.begin, operand);
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
                                     .op = tokenToUnaryOperator(tag)}});
    }

    unreachable("unreachable");
}

static AstNode *prefix(Parser *P, AstNode *(parsePrimary)(Parser *, bool))
{
    switch (current(P)->tag) {
    case tokPlusPlus:
    case tokMinusMinus:
    case tokPlus:
    case tokMinus:
    case tokLNot:
    case tokBNot:
    case tokBAnd:
    case tokLAnd:
    case tokMult:
    case tokAwait:
    case tokDelete:
        break;
    default:
        return postfix(P, parsePrimary);
    }

    const Token tok = *advance(P);
    AstNode *operand = prefix(P, parsePrimary);
    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astUnaryExpr,
                   .unaryExpr = {.operand = operand,
                                 .op = tokenToUnaryOperator(tok.tag),
                                 .isPrefix = true}});
}

static AstNode *binary(Parser *P,
                       AstNode *lhs,
                       int prec,
                       AstNode *(parsePrimary)(Parser *, bool))
{
    while (!isEoF(P)) {
        const Token tok = *current(P);
        Operator op = tokenToBinaryOperator(tok.tag);
        int nextPrecedence = getBinaryOpPrecedence(op);
        if (nextPrecedence > prec)
            break;
        if (nextPrecedence < prec)
            lhs = binary(P, lhs, nextPrecedence, parsePrimary);
        else {
            advance(P);
            AstNode *rhs = prefix(P, parsePrimary);
            lhs = newAstNode(
                P,
                &lhs->loc.begin,
                &(AstNode){.tag = astBinaryExpr,
                           .binaryExpr = {.lhs = lhs, .op = op, .rhs = rhs}});
        }
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
        AstNode *lhs = ternary(P, parsePrimary);
        consume0(P, tokColon);
        AstNode *rhs = ternary(P, parsePrimary);

        return newAstNode(
            P,
            &cond->loc.begin,
            &(AstNode){
                .tag = astTernaryExpr,
                .ternaryExpr = {.cond = cond, .ifTrue = lhs, .ifFalse = rhs}});
    }

    return cond;
}

static AstNode *stringExpr(Parser *P)
{
    const Token tok = *consume0(P, tokLString);
    AstNodeList parts = {NULL};
    while (!check(P, tokRString) && !isEoF(P)) {
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
    Token tok = *current(P);
    bool isVariadic = match(P, tokElipsis) != NULL;
    const char *name = getTokenString(P, consume0(P, tokIdent));
    consume0(P, tokColon);
    AstNode *type = parseType(P), *def = NULL;
    if (match(P, tokEqual)) {
        def = expression(P, false);
    }

    return newAstNode(P,
                      &tok.fileLoc.begin,
                      &(AstNode){.tag = astFuncParam,
                                 .funcParam = {.isVariadic = isVariadic,
                                               .name = name,
                                               .type = type,
                                               .def = def}});
}

static AstNode *closure(Parser *P)
{
    AstNode *ret = NULL, *body = NULL;

    Token tok = *consume0(P, tokLParen);
    AstNode *params = parseMany(P, tokRParen, tokComma, functionParam);
    consume0(P, tokLParen);

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
    AstNode *args = parseAtLeastOne(P, msg, tokRParen, tokComma, with);
    consume0(P, tokRParen);

    return create(P, &start.fileLoc.begin, args, strict);
}

static AstNode *parseTupleType(Parser *P)
{
    Token tok = *consume0(P, tokLParen);
    AstNode *elems =
        parseAtLeastOne(P, "tuple members", tokRParen, tokComma, parseType);
    consume0(P, tokRParen);
    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astTupleType, .tupleType = {.args = elems}});
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

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astGenericParam,
                   .genericParam = {.name = getTokenString(P, &tok),
                                    .constraints = constraints.first}});
}

static AstNode *parseFuncParam(Parser *P)
{
    Token tok = *current(P);
    bool isVariadic = match(P, tokElipsis) != NULL;
    const char *name = getTokenString(P, consume0(P, tokIdent));
    consume0(P, tokColon);
    AstNode *type = parseType(P), *value = NULL;
    if (match(P, tokAssign)) {
        value = expression(P, true);
    }

    return newAstNode(P,
                      &tok.fileLoc.begin,
                      &(AstNode){.tag = astFuncParam,
                                 .funcParam = {.isVariadic = isVariadic,
                                               .name = name,
                                               .def = value}});
}

// async func(a: T, ...b:T[]) -> T;
static AstNode *parseFuncType(Parser *P)
{
    AstNode *gParams = NULL, *params = NULL, *ret = NULL;
    Token tok = *current(P);
    bool isAsync = match(P, tokAsync) != NULL;
    consume0(P, tokFunc);
    if (match(P, tokLBracket)) {
        gParams = parseMany(P, tokRBracket, tokComma, parseGenericParam);
        consume0(P, tokRBracket);
    }

    consume0(P, tokLParen);
    params = parseMany(P, tokRParen, tokComma, parseFuncParam);
    consume0(P, tokRParen);

    consume0(P, tokThinArrow);
    ret = parseType(P);

    return newAstNode(P,
                      &tok.fileLoc.begin,
                      &(AstNode){.tag = astFuncType,
                                 .funcType = {.isAsync = isAsync,
                                              .genericParams = gParams,
                                              .params = params,
                                              .ret = ret}});
}

static AstNode *createTupleOrGroupExpression(Parser *P,
                                             const FilePos *begin,
                                             AstNode *node,
                                             bool orGroup)
{

    if (node->next == NULL && orGroup)
        return newAstNode(
            P, begin, &(AstNode){.tag = astGroupExpr, .groupExpr.expr = node});

    return newAstNode(
        P, begin, &(AstNode){.tag = astTupleExpr, .tupleExpr.args = node});
}

static inline AstNode *expressionWithoutStructs(Parser *P)
{
    return expression(P, false);
}

static inline AstNode *expressionWithStructs(Parser *P)
{
    return expression(P, true);
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

    return newAstNode(
        P,
        &callee->loc.begin,
        &(AstNode){.tag = astMacroCallExpr,
                   .macroCallExpr = {.callee = callee, .args = args}});
}

static AstNode *callExpression(Parser *P, AstNode *callee)
{
    consume0(P, tokLParen);
    AstNode *args = parseMany(P, tokRParen, tokComma, expressionWithStructs);
    consume0(P, tokRParen);

    return newAstNode(P,
                      &callee->loc.begin,
                      &(AstNode){.tag = astCallExpr,
                                 .callExpr = {.callee = callee, .args = args}});
}

static AstNode *block(Parser *P)
{
    AstNodeList stmts = {NULL};

    Token tok = *consume0(P, tokLBrace);
    while (!check(P, tokRBrace, tokEoF)) {
        E4C_TRY_BLOCK({
            listAddAstNode(&stmts, statement(P));
            match(P, tokSemicolon);
        } E4C_CATCH(ParserException) {
            synchronizeUntil(P, tokRBrace);
            break;
        })
    }
    consume0(P, tokRBracket);

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astBlockStmt, .blockStmt = {.stmts = stmts.first}});
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

static AstNode *pathElement(Parser *P)
{
    AstNode *args = NULL;
    Token tok = *consume0(P, tokIdent);
    const char *name = getTokenString(P, &tok);

    if (match(P, tokLBracket)) {
        args = parseMany(P, tokRBracket, tokComma, parseType);
        consume0(P, tokRBracket);
    }

    return newAstNode(P,
                      &tok.fileLoc.begin,
                      &(AstNode){.tag = astPathElem,
                                 .pathElement = {.name = name, .args = args}});
}

static AstNode *parsePath(Parser *P)
{
    AstNodeList parts = {NULL};
    Token tok = *current(P);

    do {
        listAddAstNode(&parts, pathElement(P));
        if (!check(P, tokDot) || peek(P, 1)->tag != tokIdent)
            break;
        consume0(P, tokDot);
    } while (isEoF(P));

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astPath, .path = {.elements = parts.first}});
}

static AstNode *fieldExpr(Parser *P)
{
    Token tok = *consume0(P, tokIdent);
    const char *name = getTokenString(P, &tok);
    AstNode *value = expression(P, true);

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astStructField,
                   .structField = {.name = name, .value = value}});
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
    case tokIdent: {
        AstNode *path = parsePath(P);
        if (allowStructs && check(P, tokLBrace))
            return structExpr(P, path, fieldExpr);
        return path;
    case tokAsync:
        return closure(P);
    default:
        reportUnexpectedToken(P, "a primary expression");
    }
    }

    unreachable("UNREACHABLE");
}

static AstNode *expression(Parser *P, bool allowStructs)
{
    AstNode *expr = ternary(P, primary);
    if (match(P, tokColon)) {
        AstNode *type = parseType(P);
        return newAstNode(
            P,
            &expr->loc.begin,
            &(AstNode){.tag = astTypedExpr,
                       .typedExpr = {.expr = expr, .type = type}});
    }

    return expr;
}

static AstNode *attribute(Parser *P)
{
    Token tok = *consume0(P, tokIdent);
    const char *name = getTokenString(P, &tok);
    AstNodeList params = {NULL};
    if (match(P, tokLParen) && !isEoF(P)) {
        while (!check(P, tokRParen)) {
            const char *pname = getTokenString(P, consume0(P, tokIdent));
            consume0(P, tokColon);
        }
    }
}

static AstNode *statement(Parser *P) { return NULL; }

static void synchronize(Parser *P)
{
    // skip current problematic token
    advance(P);
    while (!match(P, tokSemicolon, tokEoF)) {
        switch (current(P)->tag) {
        case tokType:
        case tokStruct:
        case tokEnum:
        case tokVar:
        case tokConst:
        case tokAsync:
        case tokFunc:
        case tokAt:
        case tokEoF:
            break;
        default:
            advance(P);
        }
    }
}

static void synchronizeUntil(Parser *P, TokenTag tag)
{
    while (!check(P, tag, tokEoF))
        advance(P);
}

Parser makeParser(Lexer *lexer, MemPool *pool)
{
    Parser parser = {.lexer = lexer, .L = lexer->log, .memPool = pool};
    parser.ahead[0] = (Token){.tag = tokEoF};
    for (u32 i = 0; i < LOOK_AHEAD; i++)
        parser.ahead[i] = advanceLexer(lexer);

    return parser;
}

AstNode *parseType(Parser *P)
{
    AstNode *node = NULL;
    Token tok = *current(P);
    if (isPrimitiveType(tok.tag)) {
        node = primitive(P);
    }
    else {
        switch (tok.tag) {
        case tokIdent:
            node = parsePath(P);
            break;
        case tokLParen:
            node = parseTupleType(P);
            break;
        case tokAsync:
        case tokFunc:
            node = parseFuncType(P);
            break;
        }
    }
}

AstNode *parseProgram(Parser *) {}
