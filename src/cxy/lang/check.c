//
// Created by Carter on 2023-03-22.
//

#include "check.h"
#include "core/alloc.h"
#include "core/utils.h"

#include <string.h>

typedef struct {
    const char *name;
    AstNode *declSite;
} Symbol;

typedef struct Scope {
    HashTable symbols;
    AstNode *node;
    struct Scope *next, *prev;
} Scope;

typedef struct {
    Log *L;
    MemPool *pool;
    FormatState *state;
    TypeTable *typeTable;
    struct {
        Scope *first, *scope;
    } env;
} CheckerContext;

static inline bool compareSymbols(const void *lhs, const void *rhs)
{
    return !strcmp(((const Symbol *)lhs)->name, ((const Symbol *)rhs)->name);
}

static Scope *newScope(Scope *prev)
{
    Scope *next = mallocOrDie(sizeof(Scope));
    next->prev = prev;
    next->next = NULL;
    next->symbols = newHashTable(sizeof(Symbol));
    if (prev)
        prev->next = next;

    return next;
}

static void freeScopes(Scope *scope)
{
    while (scope) {
        Scope *next = scope->next;
        freeHashTable(&scope->symbols);
        free(scope);
        scope = next;
    }
}

static void defineSymbol(CheckerContext *context,
                         const char *name,
                         AstNode *node)
{
    csAssert0(context->env.scope);
    if (name[0] == '_')
        return;

    Symbol symbol = {.name = name, .declSite = node};
    u32 hash = hashStr(hashInit(), name);
    bool wasInserted = insertInHashTable(&context->env.scope->symbols,
                                         &symbol,
                                         hash,
                                         sizeof(Symbol),
                                         compareSymbols);
    if (!wasInserted) {
        logError(context->L,
                 &node->loc,
                 "symbol {s} already defined",
                 (FormatArg[]){{.s = name}});
        const Symbol *prev = findInHashTable(&context->env.scope->symbols,
                                             &symbol,
                                             hash,
                                             sizeof(Symbol),
                                             compareSymbols);
        csAssert0(prev);
        logNote(
            context->L, &prev->declSite->loc, "previously declared here", NULL);
    }
}

static u64 levenshteinDistance(const char *lhs, const char *rhs, u64 minDist)
{
    if (!lhs)
        return strlen(rhs);
    if (!rhs)
        return strlen(lhs);

    if (lhs[0] == rhs[0])
        return levenshteinDistance(lhs + 1, rhs + 1, minDist);

    if (minDist == 0)
        return 1;

    u64 a = levenshteinDistance(lhs + 1, rhs, minDist - 1);
    u64 b = levenshteinDistance(lhs, rhs + 1, minDist - 1);
    u64 c = levenshteinDistance(lhs + 1, rhs + 1, minDist - 1);

    u64 min = MIN(a, b);

    return MIN(min, c);
}

static void suggestSimilarSymbol(CheckerContext *ctx, const char *name)
{
    u64 minDist = 2;

    if (strlen(name) <= minDist)
        return;

    const char *similar = NULL;
    for (Scope *scope = ctx->env.scope; scope; scope = scope->prev) {
        Symbol *symbols = scope->symbols.elems;
        for (u32 i = 0; i < scope->symbols.capacity; i++) {
            if (!isBucketOccupied(&scope->symbols, i))
                continue;
            u64 dist = levenshteinDistance(name, symbols[i].name, minDist);
            if (dist < minDist) {
                minDist = dist;
                similar = symbols[i].name;
            }
        }
    }

    if (similar) {
        logNote(
            ctx->L, NULL, "did you mean '{s}'", (FormatArg[]){{.s = similar}});
    }
}

static AstNode *findSymbol(CheckerContext *ctx,
                           const char *name,
                           const FileLoc *loc)
{
    u32 hash = hashStr(hashInit(), name);
    for (Scope *scope = ctx->env.scope; scope; scope = scope->prev) {
        Symbol *symbol = findInHashTable(&scope->symbols,
                                         &(Symbol){.name = name},
                                         hash,
                                         sizeof(Symbol),
                                         compareSymbols);
        if (symbol)
            return symbol->declSite;
    }

    logError(ctx->L, loc, "undefined symbol '{s}'", (FormatArg[]){{.s = name}});
    suggestSimilarSymbol(ctx, name);
    return NULL;
}

static inline AstNode *findEnclosingScope(CheckerContext *ctx,
                                          const char *keyword,
                                          const char *context,
                                          AstTag firstTag,
                                          AstTag secondTag,
                                          const FileLoc *loc)
{
    for (Scope *scope = ctx->env.scope; scope; scope = scope->prev) {
        if (scope->node->tag == firstTag || scope->node->tag == secondTag)
            return scope->node;
    }

    logError(ctx->L,
             loc,
             "use of '{$}{s}{$}' outside of a {s}",
             (FormatArg[]){
                 {.style = keywordStyle},
                 {.s = keyword},
                 {.style = resetStyle},
                 {.s = context},
             });
    return NULL;
}

static inline AstNode *findEnclosingLoop(CheckerContext *ctx,
                                         const char *keyword,
                                         const FileLoc *loc)
{
    return findEnclosingScope(
        ctx, keyword, "loop", astWhileStmt, astForStmt, loc);
}

static inline AstNode *findEnclosingFunc(CheckerContext *ctx,
                                         const FileLoc *loc)
{
    return findEnclosingScope(
        ctx, "return", "function", astClosureExpr, astFuncDecl, loc);
}

static void push_scope(CheckerContext *ctx, AstNode *node)
{
    if (!ctx->env.scope)
        ctx->env.scope = ctx->env.first;
    else if (ctx->env.scope->next)
        ctx->env.scope = ctx->env.scope->next;
    else
        ctx->env.scope = newScope(ctx->env.scope);
    ctx->env.scope->node = node;
    clearHashTable(&ctx->env.scope->symbols);
}

static inline void popScope(CheckerContext *ctx)
{
    csAssert0(ctx->env.scope);
    ctx->env.scope = ctx->env.scope->prev;
}

static inline const Type *evalType(AstVisitor *visitor, AstNode *node)
{
    astVisit(visitor, node);
    return node->type;
}

static inline u64 checkMany(AstVisitor *visitor, AstNode *node)
{
    u64 i = 0;
    for (; node; node = node->next, i++)
        astVisit(visitor, node);

    return i;
}

static void checkLiteral(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    switch (node->tag) {
    case astNullLit:
        node->type = makeNullType(ctx->typeTable);
        break;
    case astBoolLit:
        node->type = makePrimitiveType(ctx->typeTable, prtBool);
        break;
    case astCharLit:
        node->type = makePrimitiveType(ctx->typeTable, prtChar);
        break;
    case astIntegerLit:
        node->type = makePrimitiveType(ctx->typeTable, prtI32);
        break;
    case astFloatLit:
        node->type = makePrimitiveType(ctx->typeTable, prtF32);
        break;
    case astStringExpr:
        checkMany(visitor, node->stringExpr.parts);
    case astStringLit:
        node->type = makeStringType(ctx->typeTable);
        break;
    default:
        unreachable("UNREACHABLE");
    }
}

static inline void checkGroupExpr(AstVisitor *visitor, AstNode *node)
{
    node->type = evalType(visitor, node->groupExpr.expr);
}

static inline void checkTypedExpr(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    const Type *expr = evalType(visitor, node->typedExpr.expr);
    const Type *to = evalType(visitor, node->typedExpr.type);

    if (!(node->type = areTypesCompatible(ctx->typeTable, to, expr))) {
        logError(ctx->L,
                 &node->loc,
                 "casting expression of type '{t}' to incompatible type '{t}'",
                 (FormatArg[]){{.t = expr}, {.t = to}});

        node->type = makeErrorType(ctx->typeTable);
    }
}

static inline void checkBinaryExpr(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);

    const Type *lhs = NULL, *rhs = NULL;
    lhs = evalType(visitor, node->binaryExpr.lhs);
    rhs = evalType(visitor, node->binaryExpr.lhs);

    if (!(node->type = isBinaryOperatorSupported(
              ctx->typeTable, lhs, rhs, node->binaryExpr.op))) {
        // Unsupported operation
        logError(ctx->L,
                 &node->loc,
                 "operator '{s}' cannot be performed between type {t} and {t}",
                 (FormatArg[]){{.s = getBinaryOpString(node->binaryExpr.op)},
                               {.t = lhs},
                               {.t = rhs}});

        node->type = makeErrorType(ctx->typeTable);
    }
}

static inline void checkUnaryExpr(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);

    const Type *type = evalType(visitor, node->binaryExpr.lhs);
    if (node->unaryExpr.isPrefix)
        node->type = isPrefixOperatorSupported(
            ctx->typeTable, type, node->binaryExpr.op);
    else
        node->type = isPostfixOperatorSupported(
            ctx->typeTable, type, node->binaryExpr.op);

    if (node->type == NULL) {
        // Unsupported operation
        logError(ctx->L,
                 &node->loc,
                 "unsupported {s} operator '{s}' on type {t}",
                 (FormatArg[]){
                     {.s = node->unaryExpr.isPrefix ? "prefix" : "postfix"},
                     {.s = getUnaryOpString(node->unaryExpr.op)},
                     {.t = type}});

        node->type = makeErrorType(ctx->typeTable);
    }
}

static inline void checkAssignExpr(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);

    const Type *lhs = evalType(visitor, node->assignExpr.lhs);
    const Type *rhs = evalType(visitor, node->assignExpr.rhs);

    if (!(node->type = isAssignmentOperationSupported(
              ctx->typeTable, lhs, rhs, node->assignExpr.op))) {
        // Unsupported operation
        logError(
            ctx->L,
            &node->loc,
            "unsupported assignment operator '{s}' from type {t} to type {t}",
            (FormatArg[]){
                {.s = node->unaryExpr.isPrefix ? "prefix" : "postfix"},
                {.s = getAssignOpString(node->assignExpr.op)},
                {.t = rhs},
                {.t = lhs}});

        node->type = makeErrorType(ctx->typeTable);
    }
}

static inline void checkTernaryExpr(AstVisitor *visitor, AstNode *node)
{
    const Type *cond, *then, *otherwise;
    CheckerContext *ctx = getAstVisitorContext(visitor);

    cond = evalType(visitor, node->ternaryExpr.cond);
    if (!areTypesCompatible(
            ctx->typeTable, cond, makePrimitiveType(ctx->typeTable, prtBool))) {
        // Unsupported operation
        logError(ctx->L,
                 &node->loc,
                 "expression of type '{s}' cannot be converted to 'bool'",
                 (FormatArg[]){{.t = cond}});
    }

    then = evalType(visitor, node->ternaryExpr.body);
    otherwise = evalType(visitor, node->ternaryExpr.otherwise);

    if (!(node->type = areTypesCompatible(ctx->typeTable, then, otherwise))) {
        // Unsupported operation
        logError(
            ctx->L,
            &node->loc,
            "operands to ternary operator has different types '{t}' and '{t}'",
            (FormatArg[]){{.t = then}, {.t = otherwise}});

        node->type = makeErrorType(ctx->typeTable);
    }
}

static inline void checkTupleExpr(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    const u64 count = countAstNodes(node->tupleExpr.args);
    const Type **members = allocFromMemPool(ctx->pool, sizeof(void *) * count);
    u32 i = 0;

    for (AstNode *next = node; next; next = next->next, i++)
        members[i] = evalType(visitor, next);

    node->type = makeTupleType(ctx->typeTable, members, count);
}

static inline void checkArrayExpr(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    if (!node->arrayExpr.elements) {
        // This is the case for the statement [], the element type
        // can be anything and the size is unknown
        node->type = makeUnknownArrayType(ctx->typeTable);
        return;
    }

    u64 count = 1;
    bool hasErrors = false;
    AstNode *first = node->arrayExpr.elements;
    const Type *elementType = evalType(visitor, first);
    for (AstNode *next = first->next; next; next = next->next) {
        const Type *nextType = evalType(visitor, next);
        if (!areTypesCompatible(ctx->typeTable, elementType, nextType)) {
            logError(ctx->L,
                     &next->loc,
                     "element type '{t}' not compatible with array element "
                     "type '{t}'",
                     (FormatArg[]){{.t = nextType}, {.t = elementType}});
            hasErrors = true;
        }
        count++;
    }

    if (hasErrors) {
        elementType = makeErrorType(ctx->typeTable);
        logNote(ctx->L, &first->loc, "array element type deduced here", NULL);
    }
    else {
        elementType = makeArrayType(ctx->typeTable, elementType, count);
    }

    node->type = elementType;
}

static inline void checkMemberExpr(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    const Type *memberType;
    const Type *target = evalType(visitor, node->memberExpr.target);
    if (node->memberExpr.member->tag == astIdentifier) {
        const char *name = node->memberExpr.member->ident.value;
        memberType = getMemberType(ctx->typeTable, target, name);
        if (!memberType) {
            logError(ctx->L,
                     &node->loc,
                     "type '{t}' has no member named '{s}'",
                     (FormatArg[]){{.t = target}, {.s = name}});
        }
    }
    else {
        const u64 index = node->memberExpr.member->intLiteral.value;
        memberType = getElementType(ctx->typeTable, target, index);
        if (!memberType) {
            logError(ctx->L,
                     &node->loc,
                     "type '{t}' has no member at index '{u64}'",
                     (FormatArg[]){{.t = target}, {.u64 = index}});
        }
    }

    if (memberType)
        node->type = memberType;
    else
        node->type = makeErrorType(ctx->typeTable);
}

static inline void checkIndexExpr(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    const Type *target = evalType(visitor, node->indexExpr.target);
    u64 indicesCount = countAstNodes(node->indexExpr.indices);

    const Type **indices = mallocOrDie(sizeof(void *) * indicesCount);
    u64 i = 0;
    for (AstNode *next = node->indexExpr.indices; next; next = next->next, i++)
        indices[i] = evalType(visitor, next);

    if (!(node->type = isIndexOperationSupported(
              ctx->typeTable, target, indices, indicesCount))) {
        logError(ctx->L,
                 &node->loc,
                 "type '{t}' does not support index operator '.[]' (operand "
                 "types are {tl})",
                 (FormatArg[]){
                     {.t = target}, {.t = indices[0]}, {.u64 = indicesCount}});

        node->type = makeErrorType(ctx->typeTable);
    }

    free(indices);
}

static inline void evalFieldExpr(AstVisitor *visitor,
                                 StructField *const field,
                                 AstNode *node)
{
    field->type = evalType(visitor, node->fieldExpr.value);
    field->name = node->fieldExpr.name;
    field->hasDefault = true;
}

static inline void checkStructExpr(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    const Type *left = evalType(visitor, node->structExpr.left);
    u64 fieldsCount = countAstNodes(node->structExpr.fields);

    bool hasErrors = false;
    StructField *fields = mallocOrDie(sizeof(StructField) * fieldsCount);
    u64 i = 0;
    for (AstNode *field = node->structExpr.fields; field;
         field = field->next, i++) {
        evalFieldExpr(visitor, &fields[i], field);
    }

    if (left->tag == astUnionDecl) {
        Type anonymousStruct =
            (Type){.tag = typStruct,
                   .tStruct = {.fields = (const StructField **)&fields,
                               .fieldsCount = fieldsCount}};
        const Type *resolved =
            findClosestUnionType(ctx->typeTable, left, &anonymousStruct);
        if (!resolved) {
            logError(ctx->L,
                     &node->loc,
                     "cannot assign struct expression to union type '{t}', no "
                     "compatible type found",
                     (FormatArg[]){{.t = left}});
            node->type = makeErrorType(ctx->typeTable);
            return;
        }

        left = resolved;
    }
    else if (left->tag != typStruct) {
        logError(ctx->L,
                 &node->loc,
                 "type '{t}' cannot be initialized with a struct expression",
                 (FormatArg[]){{.t = left}});
        node->type = makeErrorType(ctx->typeTable);
        return;
    }

    i = 0;
    for (AstNode *field = node->structExpr.fields; field;
         field = field->next, i++) {
        if (isErrorType(ctx->typeTable, fields[i].type)) {
            hasErrors = true;
            continue;
        }

        const char *name = fields[i].name;
        const Type *fieldType = getMemberType(ctx->typeTable, left, name);

        if (fieldType == NULL) {
            hasErrors = true;
            logError(ctx->L,
                     &field->loc,
                     "struct '{t}' has no member named '{s}'",
                     (FormatArg[]){{.t = left}, {.s = name}});
            continue;
        }

        if (areTypesCompatible(ctx->typeTable, fieldType, fields[i].type)) {
            hasErrors = true;
            logError(ctx->L,
                     &field->loc,
                     "incompatible type when initializing field '{s}' of type "
                     "'{t}' with type '{t}'",
                     (FormatArg[]){
                         {.s = name}, {.t = fieldType}, {.t = fields[i].type}});
        }
    }

    node->type = hasErrors ? makeErrorType(ctx->typeTable) : left;
}

static inline void checkCallExpr(AstVisitor *visitor, AstNode *node)
{
    const AstNode *func = NULL;
    CheckerContext *ctx = getAstVisitorContext(visitor);
    const Type *callee = evalType(visitor, node->callExpr.callee);
    u64 argsCount = checkMany(visitor, node->callExpr.args);

    node->type = makeErrorType(ctx->typeTable);

    if (isErrorType(ctx->typeTable, callee)) {
        return;
    }

    if (callee->tag != typFunc) {
        logError(ctx->L,
                 &node->loc,
                 "expression of type '{t}' cannot be as a function type",
                 (FormatArg[]){{.t = callee}});

        return;
    }
    func = callee->func.decl;

    u64 paramsCount =
        callee->func.isVariadic ? callee->func.count - 1 : callee->func.count;
    if (argsCount < paramsCount) {
        logError(ctx->L,
                 &node->callExpr.callee->loc,
                 "to few arguments defined to function '{s}'",
                 (FormatArg[]){{.s = callee->func.name}});
        logNote(ctx->L, &func->loc, "function declared here", NULL);
    }
    else if (!callee->func.isVariadic && argsCount > paramsCount) {
        logError(ctx->L,
                 &node->callExpr.callee->loc,
                 "to many arguments defined to function '{s}'",
                 (FormatArg[]){{.s = callee->func.name}});
        logNote(ctx->L, &func->loc, "function declared here", NULL);
    }

    u64 i = 0;
    const AstNode *param = callee->func.decl->funcDecl.params;
    const AstNode *arg = node->callExpr.args;

    for (; param; param = param->next, arg = arg->next, i++) {
        if (!areTypesCompatible(ctx->typeTable, param->type, arg->type)) {
            logError(ctx->L,
                     &arg->loc,
                     "incompatible type for argument {u64} of function '{s}'",
                     (FormatArg[]){{.u64 = i + 1}, {.s = callee->func.name}});
            logNote(ctx->L,
                    &param->loc,
                    "expecting type of '{t}' but got argument of type '{t}'",
                    (FormatArg[]){{.t = param->type}, {.t = arg->type}});
        }
    }

    node->type = func->funcDecl.ret->type;
}

void typeCheck(AstNode *program, FormatState *state)
{
    CheckerContext context = {.state = state};
    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context,{
        [astNullLit] = checkLiteral,
        [astBoolLit] = checkLiteral,
        [astCharLit] = checkLiteral,
        [astIntegerLit] = checkLiteral,
        [astFloatLit] = checkLiteral});
    // clang-format on
}