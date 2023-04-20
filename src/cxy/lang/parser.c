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

static void synchronize(Parser *P);
static void synchronizeUntil(Parser *P, TokenTag tag);
static AstNode *expression(Parser *P, bool allowStructs);
static AstNode *statement(Parser *P);
static AstNode *parseType(Parser *P);

static AstNode *primary(Parser *P, bool allowStructs);
static AstNode *macroExpression(Parser *P, AstNode *callee);
static AstNode *callExpression(Parser *P, AstNode *callee);
static AstNode *parsePath(Parser *P);
static AstNode *variable(
    Parser *P, bool isPublic, bool isExport, bool isExpression, bool woInit);
static AstNode *funcDecl(Parser *P, bool isPublic, bool isNative);

static AstNode *aliasDecl(Parser *P, bool isPublic, bool isNative);
static AstNode *enumDecl(Parser *P, bool isPublic);
static AstNode *structDecl(Parser *P, bool isPublic);
static AstNode *attributes(Parser *P);

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
        parser->ahead[3] = advanceLexer(parser->lexer);
    }

    return previous(parser);
}

static inline Token *peek(Parser *parser, u32 index)
{
    csAssert(index <= 2, "size out of bounds");
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
({ TokenTag LINE_VAR(tags)[] = { __VA_ARGS__, tokEoF }; parserCheck((P), LINE_VAR(tags), sizeof__(LINE_VAR(tags))-1); })

#define match(P, ...) \
({ TokenTag LINE_VAR(mtags)[] = { __VA_ARGS__, tokEoF }; parserMatch((P), LINE_VAR(mtags), sizeof__(LINE_VAR(mtags))-1); })

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
        &(AstNode){.tag = astCharLit, .charLiteral.value = tok->cVal});
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
        &(AstNode){.tag = astFloatLit, .floatLiteral.value = tok->fVal});
}

static inline AstNode *parseString(Parser *P)
{
    const Token tok = *consume0(P, tokStringLiteral);
    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astStringLit,
                   .stringLiteral.value = getTokenString(P, &tok, true)});
}

static inline AstNode *parseIdentifier(Parser *P)
{
    const Token tok = *consume0(P, tokIdent);
    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astIdentifier,
                   .ident.value = getTokenString(P, &tok, false)});
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
    else
        member = parseIdentifier(P);

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
            operand = member(P, &tok.fileLoc.begin, operand);
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
                                     .op = tokenToUnaryOperator(tag)}});
    }

    unreachable("unreachable");
}

static AstNode *fieldExpr(Parser *P);
static AstNode *structExpr(Parser *P,
                           AstNode *lhs,
                           AstNode *(parseField)(Parser *));

static AstNode *functionParam(Parser *P);

static AstNode *newOperator(Parser *P, AstNode *(parsePrimary)(Parser *, bool))
{
    Token tok = *current(P);
    AstNode *type = NULL;
    AstNode *init = NULL;
    if (match(P, tokAuto)) {
        init = parsePrimary(P, true);
    }
    else {
        type = parseType(P);
        if (match(P, tokLParen)) {
            init = parseMany(P, tokRParen, tokComma, functionParam);
            consume0(P, tokRParen);
        }

        init =
            makeAstNode(P->memPool,
                        &tok.fileLoc,
                        &(AstNode){.tag = astCallExpr,
                                   .flags = type->flags,
                                   .callExpr = {.callee = type, .args = init}});
        type = NULL;
    }
    return makeAstNode(
        P->memPool,
        &tok.fileLoc,
        &(AstNode){.tag = astNewExpr, .newExpr = {.type = type, .init = init}});
}

static AstNode *prefix(Parser *P, AstNode *(parsePrimary)(Parser *, bool))
{
    bool isBand = check(P, tokBAnd);
    switch (current(P)->tag) {
#define f(O, T, ...) case tok##T:
        AST_PREFIX_EXPR_LIST(f)
#undef f
        break;
    default:
        return postfix(P, parsePrimary);
    }

    const Token tok = *advance(P);
    AstNode *operand;
    if (tok.tag == tokNew)
        return newOperator(P, parsePrimary);
    else
        operand = prefix(P, parsePrimary);

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
            &(AstNode){.tag = astAddressOf, .unaryExpr = {.operand = operand}});
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
        Operator op = tokenToBinaryOperator(tok.tag);
        if (op == opInvalid)
            break;

        int nextPrecedence = getBinaryOpPrecedence(op);
        if (nextPrecedence > prec)
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
        AstNode *lhs = ternary(P, parsePrimary);
        consume0(P, tokColon);
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
    AstNode *type = parseType(P), *def = NULL;
    if (match(P, tokAssign)) {
        def = expression(P, false);
    }

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astFuncParam,
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
    Token tok = *consume0(P, tokRange);
    consume0(P, tokLParen);
    start = expressionWithoutStructs(P);
    consume0(P, tokComma);
    end = expressionWithoutStructs(P);
    if (match(P, tokComma)) {
        step = expressionWithoutStructs(P);
    }
    consume0(P, tokRParen);

    return makeAstNode(
        P->memPool,
        &tok.fileLoc,
        &(AstNode){.tag = astRangeExpr,
                   .rangeExpr = {.start = start, .end = end, .step = step}});
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

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astGenericParam,
                   .genericParam = {.name = getTokenString(P, &tok, false),
                                    .constraints = constraints.first}});
}

// async func(a: T, ...b:T[]) -> T;
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

    if (node->next == NULL && orGroup)
        return newAstNode(
            P, begin, &(AstNode){.tag = astGroupExpr, .groupExpr.expr = node});

    return newAstNode(
        P, begin, &(AstNode){.tag = astTupleExpr, .tupleExpr.args = node});
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
    consume0(P, tokRBrace);

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

static AstNode *parseTypeOrIndex(Parser *P)
{
    if (check(P, tokIntLiteral))
        return parseInteger(P);
    return parseType(P);
}

static AstNode *pathElement(Parser *P)
{
    AstNode *args = NULL;
    Token tok = *consume0(P, tokIdent);
    const char *name = getTokenString(P, &tok, false);

    if (match(P, tokLBracket)) {
        args = parseMany(P, tokRBracket, tokComma, parseTypeOrIndex);
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
    AstNode *expr = ternary(P, primary);
    if (!P->inCase && match(P, tokColon)) {
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
    const char *name = getTokenString(P, &tok, false);
    AstNodeList args = {NULL};
    if (match(P, tokLParen) && !isEoF(P)) {
        while (!check(P, tokRParen, tokEoF)) {
            AstNode *value = NULL;
            Token start = *consume0(P, tokIdent);
            const char *pname = getTokenString(P, &start, false);
            consume0(P, tokColon);

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

            listAddAstNode(
                &args,
                newAstNode(
                    P,
                    &start.fileLoc.begin,
                    &(AstNode){.tag = astFieldExpr,
                               .fieldExpr = {.name = pname, .value = value}}));

            match(P, tokComma);
        }
        consume0(P, tokRParen);
    }

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astAttr, .attr = {.name = name, .args = args.first}});
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

static AstNode *variable(
    Parser *P, bool isPublic, bool isNative, bool isExpression, bool woInit)
{
    Token tok = *current(P);
    uint64_t flags = isPublic ? flgPublic : flgNone;
    flags |= isNative ? flgNative : flgNone;
    flags |= tok.tag == tokConst ? flgConst : flgNone;

    if (!match(P, tokConst, tokVar))
        reportUnexpectedToken(P, "var/const to start variable declaration");

    AstNode *names = NULL, *type = NULL, *init = NULL;
    if (match(P, tokLParen)) {
        names = parseAtLeastOne(
            P, "variable names", tokRParen, tokComma, parseIdentifier);
        consume0(P, tokRParen);
    }
    else {
        names = parseIdentifier(P);
    }

    if (!isExpression && (match(P, tokColon) != NULL))
        type = parseType(P);

    if (!isNative && !woInit) {
        if (tok.tag == tokConst)
            consume0(P, tokAssign);
        if (tok.tag == tokConst || match(P, tokAssign) || isExpression)
            init = expression(P, true);
    }

    if (!(isExpression || woInit || isNative)) {
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

static AstNode *macroDecl(Parser *P, bool isPublic)
{
    AstNode *params = NULL, *body = NULL, *ret = NULL;
    Token tok = *current(P);
    consume0(P, tokMacro);
    cstring name = getTokenString(P, consume0(P, tokIdent), false);

    consume0(P, tokLParen);
    params = parseMany(P, tokRParen, tokComma, functionParam);
    consume0(P, tokRParen);

    if (match(P, tokColon))
        ret = parseType(P);

    body = block(P);

    return newAstNode(P,
                      &tok.fileLoc.begin,
                      &(AstNode){.tag = astMacroDecl,
                                 .flags = isPublic ? flgPublic : flgNone,
                                 .macroDecl = {.name = name,
                                               .params = params,
                                               .ret = ret,
                                               .body = body}});
}

typedef Pair(Operator, cstring) OperatorOverload;
static OperatorOverload operatorOverload(Parser *P)
{
    OperatorOverload op = {};
    consume0(P, tokQuote);
    if (match(P, tokLBracket)) {
        consume0(P, tokRBracket);
        if (match(P, tokAssign)) {
            op = (OperatorOverload){.f = opIndexAssignOverload,
                                    .s = "op_idx_assign"};
        }
        else {
            op = (OperatorOverload){.f = opIndexOverload, .s = "op_idx"};
        }
    }
    else if (match(P, tokLParen)) {
        op = (OperatorOverload){.f = opCallOverload, .s = "op_call"};
        consume0(P, tokRParen);
    }
    else if (match(P, tokIdent)) {
        Token ident = *previous(P);
        cstring name = getTokenString(P, &ident, false);
        if (strcmp(name, "str") == 0) {
            op = (OperatorOverload){.f = opStringOverload, .s = "op_str"};
        }
        else {
            parserError(P,
                        &ident.fileLoc,
                        "unexpected operator overload `{s}`",
                        (FormatArg[]){{.s = name}});
        }
    }
    else {
        switch (current(P)->tag) {
        case tokNew:
            op = (OperatorOverload){.f = opNew, .s = "op_new"};
            break;
        case tokDelete:
            op = (OperatorOverload){.f = opDelete, .s = "op_delete"};
            break;

#define f(O, P, T, S, N)                                                       \
    case tok##T:                                                               \
        op = (OperatorOverload){.f = op##O, .s = "op_" N};                     \
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

static AstNode *funcDecl(Parser *P, bool isPublic, bool isNative)
{
    AstNode *gParams = NULL, *params = NULL, *ret = NULL, *body = NULL;
    Token tok = *current(P);
    u64 flags = isPublic ? flgPublic : flgNone;
    flags |= isNative ? flgNative : flgNone;
    flags |= match(P, tokAsync) ? flgAsync : flgNone;

    consume0(P, tokFunc);
    cstring name = NULL;
    Operator op = opInvalid;
    if (check(P, tokQuote)) {
        OperatorOverload overload = operatorOverload(P);
        op = overload.f;
        name = overload.s;
    }
    else {
        name = getTokenString(P, consume0(P, tokIdent), false);
    }

    if (match(P, tokLBracket)) {
        if (isNative)
            reportUnexpectedToken(
                P, "a '(', native functions cannot have generic parameters");

        gParams = parseAtLeastOne(
            P, "generic params", tokRBracket, tokComma, parseGenericParam);
        consume0(P, tokRBracket);
    }

    consume0(P, tokLParen);
    params = parseMany(P, tokRParen, tokComma, functionParam);
    consume0(P, tokRParen);

    if (match(P, tokColon))
        ret = parseType(P);
    else if (isNative)
        reportUnexpectedToken(P, "colon before native function return type");

    if (!isNative) {
        if (match(P, tokFatArrow)) {
            body = expression(P, true);
        }
        else {
            body = block(P);
        }
    }
    else {
        consume(P,
                tokSemicolon,
                "';', native function declaration must be terminated with a "
                "semicolon",
                NULL);
    }

    AstNode *func = newAstNode(P,
                               &tok.fileLoc.begin,
                               &(AstNode){.tag = astFuncDecl,
                                          .flags = flags,
                                          .funcDecl = {.name = name,
                                                       .operatorOverload = op,
                                                       .params = params,
                                                       .ret = ret,
                                                       .body = body}});
    if (gParams) {
        return newAstNode(
            P,
            &tok.fileLoc.begin,
            &(AstNode){.tag = astGenericDecl,
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

    body = statement(P);
    if (match(P, tokElse)) {
        ifElse = statement(P);
    }

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){
            .tag = astIfStmt,
            .ifStmt = {.cond = cond, .body = body, .otherwise = ifElse}});
}

static AstNode *forStatement(Parser *P)
{
    AstNode *body = NULL;
    Token tok = *consume0(P, tokFor);

    consume0(P, tokLParen);
    AstNode *var = variable(P, false, false, true, true);
    consume0(P, tokColon);
    AstNode *range = expression(P, true);
    consume0(P, tokRParen);
    if (!match(P, tokSemicolon))
        body = statement(P);

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
        body = statement(P);

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
    if (match(P, tokCase)) {
        P->inCase = true;
        match = expression(P, false);
        P->inCase = false;
    }
    else {
        consume(
            P, tokDefault, "expecting a 'default' or a 'case' statement", NULL);
        flags |= flgDefault;
    }

    consume0(P, tokColon);
    if (!check(P, tokCase))
        body = statement(P);

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
        listAddAstNode(&cases, caseStatement(P));
    }
    consume0(P, tokRBrace);

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astSwitchStmt,
                   .switchStmt = {.cond = cond, .cases = cases.first}});
}

static AstNode *deferStatement(Parser *P)
{
    AstNode *expr = NULL;
    Token tok = *consume0(P, tokDefer);
    bool isBlock = check(P, tokLBrace) != NULL;
    expr = expression(P, true);
    if (!isBlock)
        consume0(P, tokSemicolon);

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
    Token *tok = match(P, tokBreak, tokContinue);
    if (tok == NULL) {
        reportUnexpectedToken(P, "continue/break");
    }
    match(P, tokSemicolon);
    return newAstNode(P,
                      &tok->fileLoc.begin,
                      &(AstNode){.tag = tok->tag == tokContinue
                                            ? astContinueStmt
                                            : astBreakStmt});
}

static AstNode *statement(Parser *P)
{
    switch (current(P)->tag) {
    case tokIf:
        return ifStatement(P);
    case tokFor:
        return forStatement(P);
    case tokSwitch:
        return switchStatement(P);
    case tokWhile:
        return whileStatement(P);
    case tokDefer:
        return deferStatement(P);
    case tokReturn:
        return returnStatement(P);
    case tokBreak:
    case tokContinue:
        return continueStatement(P);
    case tokVar:
    case tokConst:
        return variable(P, false, false, false, false);
    case tokFunc:
        return funcDecl(P, false, false);
    case tokLBrace:
        return block(P);
    default: {
        AstNode *expr = expression(P, false);
        expr = newAstNode(
            P,
            &expr->loc.begin,
            &(AstNode){.tag = astExprStmt, .exprStmt = {.expr = expr}});
        match(P, tokSemicolon);
        return expr;
    }
    }
}

static AstNode *parseType(Parser *P)
{
    AstNode *type;
    Token tok = *current(P);
    if (isPrimitiveType(tok.tag)) {
        type = primitive(P);
    }
    else {
        switch (tok.tag) {
        case tokIdent:
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
            type = makeAstNode(
                P->memPool,
                &tok.fileLoc,
                &(AstNode){.tag = astIdentifier, .ident.value = "char"});
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

    return type;
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

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){
            .tag = astStructField,
            .flags = isPrivate ? flgPrivate : flgNone,
            .structField = {.name = name, .type = type, .value = value}});
}

static AstNode *parseStructMember(Parser *P)
{
    AstNode *member = NULL, *attrs = NULL;
    Token tok = *current(P);

    if (check(P, tokAt))
        attrs = attributes(P);

    bool isPrivate = match(P, tokMinus);
    bool isConst = match(P, tokConst);

    switch (current(P)->tag) {
    case tokIdent:
        member = parseStructField(P, isPrivate);
        break;
    case tokFunc:
    case tokAsync:
        member = funcDecl(P, !isPrivate, false);
        break;
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
        member = structDecl(P, !isPrivate);
        break;
    default:
        reportUnexpectedToken(P, "struct member");
    }
    member->flags |= (isConst ? flgConst : flgNone);
    member->attrs = attrs;
    return member;
}

static AstNode *structDecl(Parser *P, bool isPublic)
{
    AstNode *base = NULL, *gParams = NULL;
    AstNodeList members = {NULL};
    Token tok = *consume0(P, tokStruct);
    cstring name = getTokenString(P, consume0(P, tokIdent), false);

    if (match(P, tokLBracket)) {
        gParams = parseAtLeastOne(
            P, "generic type params", tokRBracket, tokComma, parseGenericParam);
        consume0(P, tokRBracket);
    }

    if (match(P, tokColon))
        base = parseType(P);

    consume0(P, tokLBrace);
    while (!check(P, tokRBrace, tokEoF)) {
        listAddAstNode(&members, parseStructMember(P));
    }
    consume0(P, tokRBrace);

    AstNode *node = newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astStructDecl,
                   .flags = isPublic ? flgPublic : flgNone,
                   .structDecl = {
                       .name = name, .base = base, .members = members.first}});

    if (gParams) {
        return newAstNode(
            P,
            &tok.fileLoc.begin,
            &(AstNode){.tag = astGenericDecl,
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
                      &(AstNode){.tag = astEnumOption,
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

static AstNode *aliasDecl(Parser *P, bool isPublic, bool isNative)
{
    AstNode *alias = NULL;
    Token tok = *consume0(P, tokType);
    u64 flags = isPublic ? flgPublic : flgNone;
    flags |= isNative ? flgNative : flgNone;
    cstring name = getTokenString(P, consume0(P, tokIdent), false);
    if (!isNative) {
        consume0(P, tokAssign);
        AstNodeList members = {NULL};
        do {
            listAddAstNode(&members, parseType(P));
        } while (match(P, tokBOr));
        alias = members.first;
    }
    else {
        consume0(P, tokSemicolon);
    }
    if (alias && alias->next) {
        return newAstNode(
            P,
            &tok.fileLoc.begin,
            &(AstNode){.tag = astUnionDecl,
                       .flags = flags,
                       .unionDecl = {.name = name, .members = alias}});
    }
    else {
        return newAstNode(
            P,
            &tok.fileLoc.begin,
            &(AstNode){.tag = astTypeDecl,
                       .flags = flags,
                       .typeDecl = {.name = name, .aliased = alias}});
    }
}

static AstNode *declaration(Parser *P)
{
    Token tok = *current(P);
    AstNode *attrs = NULL, *decl = NULL;
    if (check(P, tokAt))
        attrs = attributes(P);
    bool isPublic = match(P, tokPub) != NULL;
    bool isNative = false;
    if (isPublic && check(P, tokNative)) {
        // do we need to consume native
        switch (peek(P, 1)->tag) {
        case tokType:
        case tokVar:
        case tokConst:
        case tokFunc:
            advance(P);
            isNative = true;
            break;
        default:
            break;
        }
    }

    switch (current(P)->tag) {
    case tokStruct:
        decl = structDecl(P, isPublic);
        break;
    case tokEnum:
        decl = enumDecl(P, isPublic);
        break;
    case tokType:
        decl = aliasDecl(P, isPublic, isNative);
        break;
    case tokVar:
    case tokConst:
        decl = variable(P, isPublic, isNative, false, false);
        break;
    case tokFunc:
    case tokAsync:
        decl = funcDecl(P, isPublic, isNative);
        break;
    case tokMacro:
        if (attrs)
            parserError(P,
                        &tok.fileLoc,
                        "attributes cannot be attached to macro declarations",
                        NULL);
        decl = macroDecl(P, isPublic);
        break;
    case tokNative:
        parserError(P,
                    &current(P)->fileLoc,
                    "native can only be used on top level struct, function or "
                    "variable declarations",
                    NULL);
        break;
    default:
        reportUnexpectedToken(P, "a declaration");
    }

#undef isNative

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
        case tokEnum:
        case tokVar:
        case tokConst:
        case tokAsync:
        case tokFunc:
        case tokAt:
        case tokEoF:
            return;
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
    for (u32 i = 1; i < TOKEN_BUFFER; i++)
        parser.ahead[i] = advanceLexer(lexer);

    return parser;
}

AstNode *parseProgram(Parser *P)
{
    Token tok = *current(P);

    AstNodeList decls = {NULL};
    while (!isEoF(P)) {
        E4C_TRY_BLOCK({
            listAddAstNode(&decls, declaration(P));
        } E4C_CATCH(ParserException) { synchronize(P); })
    }

    return newAstNode(
        P,
        &tok.fileLoc.begin,
        &(AstNode){.tag = astProgram, .program = {.decls = decls.first}});
}
