//
// Created by Carter on 2023-03-28.
//

#include "tcheck.h"
#include "ttable.h"

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
    TypeTable *typeTable;
    struct {
        Scope *first, *scope;
    } env;
} CheckerContext;

static const Type *sError;

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

static void pushScope(CheckerContext *ctx, AstNode *node)
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
    CheckerContext *ctx = getAstVisitorContext(visitor);
    astVisit(visitor, node);
    return resolveType(ctx->typeTable, node->type);
}

static inline u64 checkMany(AstVisitor *visitor, AstNode *node)
{
    u64 i = 0;
    for (; node; node = node->next, i++)
        astVisit(visitor, node);

    return i;
}

static void checkProgram(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    pushScope(ctx, node);
    checkMany(visitor, node->program.decls);
}

static void checkFallback(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    node->type = makeVoidType(ctx->typeTable);
}

static void checkLiterals(AstVisitor *visitor, AstNode *node)
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
    case astStringLit:
        node->type = makeStringType(ctx->typeTable);
        break;
    default:
        csAssert0("Not a literal");
    }
}

static void checkStringExpr(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    checkMany(visitor, node->stringExpr.parts);
    const AstNode *part = node->stringExpr.parts;
    for (; part; part = part->next) {
        if (part->type == sError) {
            node->type = sError;
        }
    }

    node->type = makeStringType(ctx->typeTable);
}

static void checkFuncParam(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    defineSymbol(ctx, node->funcParam.name, node);
    node->type = evalType(visitor, node->funcParam.type);
    if (node->funcParam.def) {
        const Type *def = evalType(visitor, node->funcParam.def);
        if (isTypeAssignableFrom(ctx->typeTable, node->type, def)) {
            logError(ctx->L,
                     &node->funcParam.def->loc,
                     "parameter default value type '{t}' not compatible with "
                     "parameter type '{t}",
                     (FormatArg[]){{.t = def}, {.t = node->type}});
        }
    }
}

static void checkFuncDecl(AstVisitor *visitor, AstNode *node)
{
    const Type *ret, **params;
    CheckerContext *ctx = getAstVisitorContext(visitor);
    u64 paramsCount = countAstNodes(node->funcDecl.params);
    AstNode *param = node->funcDecl.params;
    u64 i = 0;
    bool isVariadic = false;

    defineSymbol(ctx, node->funcDecl.name, node);

    pushScope(ctx, node);
    params = mallocOrDie(sizeof(Type *) * paramsCount);
    for (; param; param = param->next) {
        param->parentScope = node;
        params[i] = evalType(visitor, param);
        if (isVariadic && param->funcParam.isVariadic) {
            logError(ctx->L,
                     &param->loc,
                     "variadic parameters should the last parameter type in "
                     "function declaration",
                     NULL);
        }
        isVariadic = param->funcParam.isVariadic;
    }

    if (node->funcDecl.ret)
        evalType(visitor, node->funcDecl.ret);

    node->funcDecl.body->parentScope = node;
    ret = evalType(visitor, node->funcDecl.body);

    node->type = makeFuncType(ctx->typeTable,
                              node->funcDecl.name,
                              isVariadic,
                              ret,
                              params,
                              paramsCount);

    free((void *)params);

    popScope(ctx);
}

static void checkClosure(AstVisitor *visitor, AstNode *node)
{
    const Type *ret, **params;
    CheckerContext *ctx = getAstVisitorContext(visitor);
    u64 paramsCount = countAstNodes(node->closureExpr.params);
    AstNode *param = node->closureExpr.params;
    u64 i = 0;

    pushScope(ctx, node);
    params = mallocOrDie(sizeof(Type *) * paramsCount);
    for (; param; param = param->next) {
        param->parentScope = node;
        params[i] = evalType(visitor, param);
        if (param->funcParam.isVariadic) {
            logError(ctx->L,
                     &param->loc,
                     "variadic parameters are not supported on closures",
                     NULL);
        }
    }

    if (node->closureExpr.ret)
        evalType(visitor, node->closureExpr.ret);

    node->closureExpr.body->parentScope = node;
    ret = evalType(visitor, node->closureExpr.body);

    node->type =
        makeFuncType(ctx->typeTable, NULL, false, ret, params, paramsCount);

    free((void *)params);

    popScope(ctx);
}

static void checkVarDecl(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    node->type = node->varDecl.type ? evalType(visitor, node->varDecl.type)
                                    : makeAutoType(ctx->typeTable);
    const Type *value = NULL;
    if (node->varDecl.init) {
        value = evalType(visitor, node->varDecl.init);
        if (!isTypeAssignableFrom(ctx->typeTable, node->type, value)) {
            logError(ctx->L,
                     &node->varDecl.init->loc,
                     "incompatible types, expecting type '{t}', got '{t}'",
                     (FormatArg[]){{.t = node->type}, {.t = value}});
        }
        else {
            node->type = value;
        }
    }

    defineSymbol(ctx, node->varDecl.names->ident.value, node);
}

static void checkIdentifier(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    AstNode *symbol = findSymbol(ctx, node->ident.value, &node->loc);
    if (symbol == NULL)
        node->type = sError;
    else
        node->type = symbol->type;
}

static void checkPathElement(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    AstNode *symbol = findSymbol(ctx, node->pathElement.name, &node->loc);
    if (symbol == NULL)
        node->type = sError;
    else
        node->type = symbol->type;
}

static void checkPath(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    AstNode *element = node->path.elements;
    const Type *type;
    for (; element; element = element->next) {
        // TODO, change scope
        type = evalType(visitor, element);
    }
    node->type = type;
}

static void checkBinary(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    const Type *left = evalType(visitor, node->binaryExpr.lhs);
    const Type *right = evalType(visitor, node->binaryExpr.rhs);
    // TODO check compatibility
    if (isTypeAssignableFrom(ctx->typeTable, left, right)) {
        node->type = left;
    }
    else {
        logError(ctx->L,
                 &node->loc,
                 "binary operator '{s}' not support between type '{t}' and "
                 "type '{t}'",
                 (FormatArg[]){{.s = getBinaryOpString(node->binaryExpr.op)},
                               {.t = left},
                               {.t = right}});
        node->type = sError;
    }
}

static void checkUnary(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    const Type *operand = evalType(visitor, node->unaryExpr.operand);
    // TODO check compatibility
    node->type = operand;
}

static void checkIndex(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    const Type *target = evalType(visitor, node->indexExpr.target);
    u64 count = checkMany(visitor, node->indexExpr.indices);

    if (target->tag == typArray) {
        if (target->array.arity != count) {
            logError(ctx->L,
                     &getLastAstNode(node->indexExpr.indices)->loc,
                     "invalid number of indexes to type '{t}', expecting "
                     "'{u64}' but got '{u64}'",
                     (FormatArg[]){{.t = target},
                                   {.u64 = target->array.arity},
                                   {.u64 = count}});
        }
        node->type = target->array.elementType;
    }
    else if (target->tag == typMap) {
        if (count > 1) {
            logError(ctx->L,
                     &node->indexExpr.indices->next->loc,
                     "invalid indexes passed to map type '{t}', expecting "
                     "'1' but got '{u64}'",
                     (FormatArg[]){{.t = target}, {.u64 = count}});
        }
        node->type = target->map.value;
    }
    else if (target->tag == typStruct || target->tag == typUnion) {
        // TODO find index operator
        logWarning(ctx->L,
                   &node->indexExpr.target->loc,
                   "indexing into {s} types currently not supported",
                   (FormatArg[]){
                       {.s = target->tag == typStruct ? "struct" : "union"}});
    }
    else {
        logError(ctx->L,
                 &node->loc,
                 "index operator (.[]) not supported on type '{t}'",
                 (FormatArg[]){{.t = target}});
    }
}

static void checkMember(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    const Type *target = evalType(visitor, node->memberExpr.target);
    AstNode *member = node->memberExpr.member;

    if (member->tag == astIntegerLit) {

        if (target->tag != typTuple) {
            logError(ctx->L,
                     &node->memberExpr.target->loc,
                     "literal member expression cannot be used on type '{t}', "
                     "type is not a tuple",
                     (FormatArg[]){{.t = target}});
            node->type = sError;
            return;
        }

        if (member->intLiteral.value > target->tuple.count) {
            logError(ctx->L,
                     &member->loc,
                     "literal member '{u64}' out of range, type '{t}' has "
                     "{u64} members",
                     (FormatArg[]){{.u64 = member->intLiteral.value},
                                   {.t = target},
                                   {.u64 = target->tuple.count}});
            node->type = sError;
            return;
        }
        node->type = target->tuple.members[member->intLiteral.value];
    }
    else {
        csAssert(member->tag == astIdentifier, "TODO");
        node->type = sError;
    }
}

static void checkTupleExpr(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    u64 count = countAstNodes(node->tupleExpr.args);
    const Type **args = mallocOrDie(sizeof(Type *) * count);
    AstNode *arg = node->tupleExpr.args;

    for (u64 i = 0; arg; arg = arg->next, i++) {
        args[i] = evalType(visitor, arg);
        if (args[i] == sError)
            node->type = sError;
    }

    if (node->type == NULL) {
        node->type = makeTupleType(ctx->typeTable, args, count);
    }

    free(args);
}

static void checkBlock(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    AstNode *stmt = node->blockStmt.stmts;

    for (; stmt; stmt = stmt->next) {
        stmt->parentScope = node;
        const Type *type = evalType(visitor, stmt);
        if (stmt->tag == astReturnStmt) {
            node->type = type;
        }
    }

    if (node->type == NULL) {
        node->type = makeVoidType(ctx->typeTable);
    }
}

static void checkReturn(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    AstNode *func = findEnclosingFunc(ctx, &node->loc);
    node->type = node->returnStmt.expr
                     ? evalType(visitor, node->returnStmt.expr)
                     : makeVoidType(ctx->typeTable);
    const Type *ret =
        func && func->funcDecl.ret ? func->funcDecl.ret->type : NULL;

    if (ret && !isTypeAssignableFrom(ctx->typeTable, ret, node->type)) {
        logError(ctx->L,
                 &node->returnStmt.expr->loc,
                 "return value of type '{t}' incompatible with function return "
                 "type '{t}",
                 (FormatArg[]){{.t = node->type}, {.t = ret}});
    }
    else {
        AstNode *block = getParentScopeWithTag(node, astBlockStmt);
        if (block && block->type &&
            !isTypeAssignableFrom(ctx->typeTable, block->type, node->type)) {
            logError(ctx->L,
                     &node->returnStmt.expr->loc,
                     "inconsistent return types in auto function, '{t}' not "
                     "compatible with '{t}'",
                     (FormatArg[]){{.t = node->type}, {.t = block->type}});
        }
    }
}

static void checkPrimitiveType(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    node->type = makePrimitiveType(ctx->typeTable, node->primitiveType.id);
}

static void checkPointerType(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    node->type = makePointerType(ctx->typeTable,
                                 evalType(visitor, node->pointerType.pointed),
                                 node->pointerType.isConst);
}

static void checkArrayType(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    const Type *element = evalType(visitor, node->arrayType.elementType);
    u64 count = countAstNodes(node->arrayExpr.elements);
    const u64 *indexes = mallocOrDie(sizeof(u64) * count);
    // TODO evaluate indexes
    node->type = makeArrayType(ctx->typeTable, element, indexes, count);

    free((void *)indexes);
}

static void checkTypeDecl(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    defineSymbol(ctx, node->typeDecl.name, node);
    const Type *ref = evalType(visitor, node->typeDecl.aliased);
    node->type = makeAliasType(ctx->typeTable, ref, node->typeDecl.name);
}

static void checkUnionDecl(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    defineSymbol(ctx, node->unionDecl.name, node);

    u64 count = countAstNodes(node->unionDecl.members);
    const Type **members = mallocOrDie(sizeof(Type *) * count);

    AstNode *member = node->unionDecl.members;
    for (u64 i = 0; member; member = member->next, i++) {
        members[i] = evalType(visitor, member);
        if (members[i] == sError)
            node->type = sError;
    }

    if (node->type == NULL)
        node->type = makeUnionType(ctx->typeTable, members, count);

    free((void *)members);
}

static void checkTupleType(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);

    u64 count = countAstNodes(node->tupleType.args);
    const Type **args = mallocOrDie(sizeof(Type *) * count);

    AstNode *arg = node->tupleType.args;
    for (u64 i = 0; arg; arg = arg->next, i++) {
        args[i] = evalType(visitor, arg);
        if (args[i] == sError)
            node->type = sError;
    }

    if (node->type == NULL)
        node->type = makeTupleType(ctx->typeTable, args, count);

    free(args);
}

static void checkFuncType(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);

    const Type *ret = evalType(visitor, node->funcType.ret);
    u64 count = countAstNodes(node->funcType.params);
    const Type **params = mallocOrDie(sizeof(Type *) * count);

    AstNode *param = node->funcType.params;
    for (u64 i = 0; param; param = param->next, i++) {
        params[i] = evalType(visitor, param);
        if (params[i] == sError)
            node->type = sError;
    }

    if (node->type == NULL)
        node->type =
            makeFuncType(ctx->typeTable, NULL, false, ret, params, count);

    free(params);
}

static void checkBuiltinType(AstVisitor *visitor, AstNode *node)
{
    CheckerContext *ctx = getAstVisitorContext(visitor);
    node->type = node->tag == astVoidType ? makeVoidType(ctx->typeTable)
                                          : makeStringType(ctx->typeTable);
}

void typeCheck(AstNode *program, Log *L, MemPool *pool, TypeTable *typeTable)
{
    CheckerContext context = {.L = L,
                              .typeTable = typeTable,
                              .pool = pool,
                              .env = {.first = newScope(NULL)}};

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context,
    {
        [astProgram] = checkProgram,
        [astPathElem] = checkPathElement,
        [astPath] = checkPath,
        [astNullLit] = checkLiterals,
        [astBoolLit] = checkLiterals,
        [astCharLit] = checkLiterals,
        [astIntegerLit] = checkLiterals,
        [astFloatLit] = checkLiterals,
        [astStringLit] = checkLiterals,
        [astStringExpr] = checkStringExpr,
        [astFuncParam] = checkFuncParam,
        [astFuncDecl] = checkFuncDecl,
        [astVarDecl] = checkVarDecl,
        [astConstDecl] = checkVarDecl,
        [astIdentifier] = checkIdentifier,
        [astBinaryExpr] = checkBinary,
        [astUnaryExpr] = checkUnary,
        [astIndexExpr] = checkIndex,
        [astMemberExpr] = checkMember,
        [astTupleExpr] = checkTupleExpr,
        [astClosureExpr] = checkClosure,
        [astBlockStmt] = checkBlock,
        [astReturnStmt] = checkReturn,
        [astPrimitiveType] = checkPrimitiveType,
        [astArrayType] = checkArrayType,
        [astPointerType] = checkPointerType,
        [astTypeDecl] = checkTypeDecl,
        [astUnionDecl] = checkUnionDecl,
        [astTupleType] = checkTupleType,
        [astFuncType] = checkFuncType,
        [astVoidType] = checkBuiltinType,
        [astStringType] = checkBuiltinType
    },
    .fallback = checkFallback);
    // clang-format off

    sError = makeErrorType(context.typeTable);

    astVisit(&visitor, program);
}
