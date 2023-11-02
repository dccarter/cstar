//
// Created by Carter Mbotho on 2023-07-14.
//

#include "check.h"
#include "eval.h"

#include "lang/ast.h"
#include "lang/flag.h"
#include "lang/operations.h"
#include "lang/strings.h"
#include "lang/ttable.h"
#include "lang/types.h"
#include "lang/visitor.h"

#include "core/alloc.h"

#include <string.h>

const FileLoc *manyNodesLoc_(FileLoc *dst, AstNode *nodes)
{
    if (nodes == NULL)
        return NULL;

    if (nodes->next == NULL) {
        *dst = nodes->loc;
        return dst;
    }

    return locExtend(dst, &nodes->loc, &getLastAstNode(nodes)->loc);
}

const FileLoc *lastNodeLoc_(FileLoc *dst, AstNode *nodes)
{
    if (nodes == NULL)
        return NULL;
    *dst = getLastAstNode(nodes)->loc;
    return dst;
}

static AstNode *makeSpreadVariable(TypingContext *ctx, AstNode *expr)
{
    if (nodeIs(expr, Identifier) ||
        (nodeIs(expr, Path) && expr->path.elements->next == NULL))
        return expr;

    // Create variable for this
    return makeAstNode(
        ctx->pool,
        &expr->loc,
        &(AstNode){
            .tag = astVarDecl,
            .type = expr->type,
            .flags = expr->flags,
            .varDecl = {.names = makeGenIdent(
                            ctx->pool, ctx->strings, &expr->loc, expr->type),
                        .init = expr}});
}

void addTopLevelDeclaration(TypingContext *ctx, AstNode *node)
{
    csAssert0(ctx->root.current);

    node->next = ctx->root.current;
    if (ctx->root.previous)
        ctx->root.previous->next = node;
    else
        ctx->root.program->program.decls = node;
    ctx->root.previous = node;
}

void addTopLevelDeclarationAsNext(TypingContext *ctx, AstNode *node)
{
    csAssert0(ctx->root.current);
    node->next = ctx->root.current->next;
    ctx->root.current->next = node;
}

void addBlockLevelDeclaration(TypingContext *ctx, AstNode *node)
{
    csAssert0(ctx->block.current);

    node->next = ctx->block.current;
    if (ctx->block.previous)
        ctx->block.previous->next = node;
    else
        ctx->block.self->program.decls = node;
    ctx->block.previous = node;
}

static void checkTypeRef(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    csAssert0(node->type);
}

static void checkDefine(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *type = node->define.type, *container = node->define.container,
            *name = node->define.names;
    const Type *type_ = checkType(visitor, type);
    u64 count = 0;

    for (; name; name = name->next, count++) {
        name->type = type_;
    }

    node->type = type_;
    if (container == NULL || typeIs(type_, Error))
        return;

    cstring *names = mallocOrDie(sizeof(cstring) * count);
    name = node->define.names;
    for (u64 i = 0; name; name = name->next) {
        names[i] = name->ident.alias ?: name->ident.value;
    }
    qsort(names, count, sizeof(cstring), compareStrings);

    node->type = makeContainerType(
        ctx->types, container->ident.value, type_, names, count);

    free(names);
}

static void checkIdentifier(AstVisitor *visitor, AstNode *node)
{
    csAssert0(node->ident.resolvesTo);
    node->type = node->ident.resolvesTo->type;
}

const Type *checkType(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    if (node == NULL)
        return NULL;

    if (hasFlag(node, Comptime)) {
        node->flags &= ~flgComptime;
        bool status = evaluate(ctx->evaluator, node);

        if (!status) {
            return node->type = ERROR_TYPE(ctx);
        }
    }

    if (node->type)
        return node->type;

    astVisit(visitor, node);
    return resolveType(node->type);
}

static void checkGenericDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    bool inferrable = false;
    node->type = makeGenericType(ctx->types, node);
}

static void checkInterfaceDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *member = node->interfaceDecl.members;
    u64 membersCount = countAstNodes(member);
    NamedTypeMember *members =
        mallocOrDie(sizeof(NamedTypeMember) * membersCount);

    for (u64 i = 0; member; member = member->next, i++) {
        const Type *type = checkType(visitor, member);
        if (typeIs(type, Error)) {
            node->type = ERROR_TYPE(ctx);
            continue;
        }
        members[i] = (NamedTypeMember){
            .name = getDeclarationName(member), .type = type, .decl = member};
    }

    if (typeIs(node->type, Error))
        goto checkInterfaceMembersError;

    node->type = makeInterfaceType(ctx->types,
                                   getDeclarationName(node),
                                   members,
                                   membersCount,
                                   node,
                                   node->flags & flgTypeApplicable);

checkInterfaceMembersError:
    if (members)
        free(members);
}

static void checkImportDecl(AstVisitor *visitor, AstNode *node) {}

static void checkBlockStmt(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *stmts = node->blockStmt.stmts, *stmt = stmts;

    __typeof(ctx->block) block = ctx->block;
    ctx->block.current = NULL;
    ctx->block.self = node;
    for (; stmt; stmt = stmt->next) {
        ctx->block.previous = ctx->block.current;
        ctx->block.current = stmt;
        const Type *type = checkType(visitor, stmt);
        if (typeIs(type, Error)) {
            node->type = ERROR_TYPE(ctx);
            ctx->block = block;
            return;
        }

        if (nodeIs(stmt, ReturnStmt)) {
            if (node->type && !isTypeAssignableFrom(node->type, type)) {
                logError(ctx->L,
                         &stmt->loc,
                         "inconsistent return types within "
                         "block, got '{t}', "
                         "expecting '{t}'",
                         (FormatArg[]){{.t = type}, {.t = node->type}});
                node->type = ERROR_TYPE(ctx);
                ctx->block = block;
                return;
            }
            node->type = type;
            if (!node->blockStmt.returned && node->next) {
                logWarning(
                    ctx->L,
                    manyNodesLoc(node->next),
                    "any code declared after return statement is unreachable",
                    NULL);
            }
            node->blockStmt.returned = true;
        }

        if (hasFlag(node, BlockReturns))
            node->type = type;
    }

    ctx->block = block;

    if (node->type == NULL)
        node->type = makeAutoType(ctx->types);
}

static void checkReturnStmt(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->returnStmt.expr, *func = node->returnStmt.func,
            *ret = nodeIs(func, FuncDecl) ? func->funcDecl.signature->ret
                                          : func->closureExpr.ret;
    const Type *expr_ =
        expr ? checkType(visitor, expr) : makeVoidType(ctx->types);

    if (typeIs(expr_, Error)) {
        node->type = expr_;
        return;
    }

    if (ret) {
        if (!isTypeAssignableFrom(ret->type, expr_)) {
            logError(ctx->L,
                     &node->loc,
                     "inconsistent return type, "
                     "expecting '{t}', got '{t}'",
                     (FormatArg[]){{.t = ret->type}, {.t = expr_}});
            logNote(ctx->L,
                    &ret->loc,
                    "return type first declared or "
                    "deduced here",
                    NULL);

            node->type = ERROR_TYPE(ctx);
            return;
        }

        node->type = ret->type;

        if (expr && typeIs(ret->type, Union) && ret->type != expr_) {
            u32 idx = findUnionTypeIndex(ret->type, expr_);
            csAssert0(idx != UINT32_MAX);
            node->returnStmt.expr = makeUnionValueExpr(
                ctx->pool, &expr->loc, expr->flags, expr, idx, NULL, ret->type);
        }

        if (!hasFlag(ret->type, Optional) || hasFlag(expr_, Optional))
            return;

        const Type *target = getOptionalTargetType(ret->type);
        if (nodeIs(expr, NullLit)) {
            if (!transformOptionalNone(visitor, expr, target))
                node->type = ERROR_TYPE(ctx);
        }
        else {
            expr->type = target;
            if (!transformOptionalSome(
                    visitor, expr, copyAstNode(ctx->pool, expr)))
                node->type = ERROR_TYPE(ctx);
        }
    }
    else {
        node->type = expr_;
        ret = makeTypeReferenceNode(ctx->pool, expr_, &node->loc);
        if (nodeIs(func, FuncDecl))
            func->funcDecl.signature->ret = ret;
        else
            func->closureExpr.ret = ret;
    }
}

static void checkDeferStmt(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *type = checkType(visitor, node->deferStmt.expr);

    if (typeIs(type, Error)) {
        node->type = type;
        return;
    }

    AstNode *block = node->deferStmt.block, *expr = node->exprStmt.expr;
    if (!block->blockStmt.returned) {
        expr->flags |= flgDeferred;
        insertAstNode(&block->blockStmt.epilogue, expr);
        node->tag = astNop;
        clearAstBody(node);
    }

    node->type = makeVoidType(ctx->types);
}

static void checkBreakOrContinueStmt(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    node->type = makeVoidType(ctx->types);
}

static void checkWhileStmt(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *cond = node->whileStmt.cond, *body = node->whileStmt.body;

    const Type *cond_ = checkType(visitor, cond);
    if (typeIs(cond_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (!isTruthyType(cond_)) {
        logError(ctx->L,
                 &cond->loc,
                 "expecting a truthy type in an `while` "
                 "statement condition, "
                 "got '{t}'",
                 (FormatArg[]){{.t = cond_}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    cond_ = unwrapType(cond_, NULL);
    if (isClassOrStructType(cond_)) {
        if (!transformToTruthyOperator(visitor, cond)) {
            if (!typeIs(cond->type, Error))
                logError(ctx->L,
                         &cond->loc,
                         "expecting a struct that overloads the truthy `!!` in "
                         "an if statement condition, "
                         "got '{t}'",
                         (FormatArg[]){{.t = cond_}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
        cond_ = cond->type;
    }

    const Type *body_ = checkType(visitor, body);
    if (typeIs(body_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    node->type = body_;
}

static void checkStringExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *part = node->stringExpr.parts;

    for (; part; part = part->next) {
        part->type = checkType(visitor, part);
        if (typeIs(part->type, Error)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }
    }

    part = node->stringExpr.parts;
    if (nodeIs(part, StringLit) && part->next == NULL) {
        node->tag = astStringLit;
        node->type = part->type;
        memcpy(&node->_body, &part->_body, CXY_AST_NODE_BODY_SIZE);
    }
    else {
        node->type = makeStringType(ctx->types);
    }
}

static void checkSpreadExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->spreadExpr.expr;
    if (nodeIs(expr, TupleExpr)) {
        astVisitManyNodes(visitor, expr->tupleExpr.elements);
        getLastAstNode(expr->tupleExpr.elements)->next = node->next;
        *node = *expr->tupleExpr.elements;
        return;
    }

    const Type *type = checkType(visitor, expr);

    if (!typeIs(type, Tuple)) {
        logError(ctx->L,
                 &node->loc,
                 "spread operator `...` can only be used "
                 "with tuple expressions",
                 NULL);
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (type->tuple.count == 0) {
        node->tag = astNop;
        // node->type = makeTupleType(ctx->types, NULL, 0, flgNone);
        return;
    }

    AstNode *variable = makeSpreadVariable(ctx, expr), *parts = NULL,
            *it = NULL;
    for (u64 i = 0; i < type->tuple.count; i++) {
        const Type *type_ = type->tuple.members[i];
        AstNode *tmp = makeAstNode(
            ctx->pool,
            &node->loc,
            &(AstNode){
                .tag = astMemberExpr,
                .flags = type->flags | type_->flags,
                .type = type_,
                .memberExpr = {
                    .target = nodeIs(variable, Path)
                                  ? copyAstNode(ctx->pool, variable)
                                  : makePathFromIdent(ctx->pool,
                                                      variable->varDecl.names),
                    .member = makeAstNode(
                        ctx->pool,
                        &node->loc,
                        &(AstNode){.tag = astIntegerLit,
                                   .type = getPrimitiveType(ctx->types, prtI64),
                                   .intLiteral.value = (i16)i})}});
        if (parts == NULL)
            parts = it = tmp;
        else
            it = it->next = tmp;
    }

    it->next = node->next;
    *node = *parts;
    addBlockLevelDeclaration(ctx, variable);
}

void checkCastExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *expr = checkType(visitor, node->castExpr.expr);
    const Type *target = checkType(visitor, node->castExpr.to);
    if (!isTypeCastAssignable(target, expr)) {
        logError(ctx->L,
                 &node->loc,
                 "type '{t}' cannot be cast to type '{t}'",
                 (FormatArg[]){{.t = expr}, {.t = target}});
    }
    node->type = target;

    if (!hasFlag(target, Optional) || hasFlag(expr, Optional))
        return;

    target = getOptionalTargetType(target);
    if (nodeIs(node->castExpr.expr, NullLit)) {
        if (!transformOptionalNone(visitor, node, target)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }
    }
    else {
        node->castExpr.expr->type = target;
        if (!transformOptionalSome(visitor, node, node->castExpr.expr)) //
        {
            node->type = ERROR_TYPE(ctx);
            return;
        }
    }
}

void checkTypedExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *expr = checkType(visitor, node->typedExpr.expr);
    const Type *type = checkType(visitor, node->typedExpr.type);
    if (isTypeCastAssignable(type, expr) ||
        (isPointerType(expr) && isPointerType(type))) {
        node->type = type;
    }
    else {
        logError(ctx->L,
                 &node->loc,
                 "type '{t}' cannot be cast to type '{t}'",
                 (FormatArg[]){{.t = expr}, {.t = type}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (!hasFlag(type, Optional) || hasFlag(expr, Optional))
        return;

    type = getOptionalTargetType(type);
    if (nodeIs(node->typedExpr.expr, NullLit)) {
        if (!transformOptionalNone(visitor, node, type))
            node->type = ERROR_TYPE(ctx);
    }
    else {
        node->typedExpr.expr->type = type;
        if (!transformOptionalSome(visitor, node, node->typedExpr.expr))
            node->type = ERROR_TYPE(ctx);
    }
}

static void checkExprStmt(AstVisitor *visitor, AstNode *node)
{
    node->type = checkType(visitor, node->exprStmt.expr);
}

static void checkStmtExpr(AstVisitor *visitor, AstNode *node)
{
    node->type = checkType(visitor, node->stmtExpr.stmt);
}

static void checkGroupExpr(AstVisitor *visitor, AstNode *node)
{
    node->type = checkType(visitor, node->groupExpr.expr);
}

static void checkMacroCallExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    if (!evaluate(ctx->evaluator, node))
        node->type = ERROR_TYPE(ctx);
}

static u64 addDefineToModuleMembers(NamedTypeMember *members,
                                    u64 index,
                                    AstNode *decl,
                                    u64 builtinFlags)
{
    if (decl->define.container) {
        decl->flags |= builtinFlags;
        members[index++] = (NamedTypeMember){
            .decl = decl, .name = getDeclarationName(decl), .type = decl->type};
    }
    else {
        AstNode *name = decl->define.names;
        for (; name; name = name->next) {
            name->flags |= builtinFlags;
            members[index++] = (NamedTypeMember){
                .decl = name,
                .name = name->ident.alias ?: name->ident.value,
                .type = name->type};
        }
    }
    return index;
}

static u64 addModuleTypeMember(NamedTypeMember *members,
                               u64 index,
                               AstNode *decl,
                               u64 builtinFlags)
{
    if (nodeIs(decl, Define)) {
        return addDefineToModuleMembers(members, index, decl, builtinFlags);
    }
    else if (!nodeIs(decl, CCode)) {
        decl->flags |= builtinFlags;
        members[index++] = (NamedTypeMember){
            .decl = decl, .name = getDeclarationName(decl), .type = decl->type};
        return index;
    }

    return index;
}

static void buildModuleType(TypingContext *ctx,
                            AstNode *node,
                            bool isBuiltinModule)
{
    u64 builtinsFlags = (isBuiltinModule ? flgBuiltin : flgNone);
    u64 count = countProgramDecls(node->program.decls) +
                countProgramDecls(node->program.top),
        i = 0;

    NamedTypeMember *members = mallocOrDie(sizeof(NamedTypeMember) * count);

    AstNode *decl = node->program.top;
    for (; decl; decl = decl->next) {
        if (nodeIs(decl, ImportDecl))
            continue;
        i = addModuleTypeMember(members, i, decl, builtinsFlags);
    }

    decl = node->program.decls;
    for (; decl; decl = decl->next) {
        i = addModuleTypeMember(members, i, decl, builtinsFlags);
    }

    node->type = makeModuleType(
        ctx->types,
        isBuiltinModule ? S___builtins : node->program.module->moduleDecl.name,
        node->loc.fileName,
        members,
        i);
    free(members);
}

static void checkProgram(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *decl = node->program.decls;
    ctx->root.program = node;
    astVisit(visitor, node->program.module);
    astVisitManyNodes(visitor, node->program.top);

    for (; decl; decl = decl->next) {
        ctx->root.previous = ctx->root.current;
        ctx->root.current = decl;
        astVisit(visitor, decl);
    }

    bool isBuiltinModule = hasFlag(node, BuiltinsModule);
    if (isBuiltinModule || node->program.module) {
        buildModuleType(ctx, node, isBuiltinModule);
    }
}

static void withSavedStack(Visitor func, AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    __typeof(ctx->stack) stack = ctx->stack;

    func(visitor, node);

    ctx->stack = stack;
}

const Type *checkMaybeComptime(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    if (hasFlag(node, Comptime) && !evaluate(ctx->evaluator, node)) {
        node->type = ERROR_TYPE(ctx);
    }

    return checkType(visitor, node);
}

AstNode *checkAst(CompilerDriver *driver, AstNode *node)
{
    TypingContext context = {.L = driver->L,
                             .pool = &driver->pool,
                             .strings = &driver->strPool,
                             .types = driver->typeTable};

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context, {
        [astProgram] = checkProgram,
        [astTypeRef] = checkTypeRef,
        [astNullLit] = checkLiteral,
        [astBoolLit] = checkLiteral,
        [astCharLit] = checkLiteral,
        [astIntegerLit] = checkLiteral,
        [astFloatLit] = checkLiteral,
        [astStringLit] = checkLiteral,
        [astPrimitiveType] = checkBuiltinType,
        [astStringType] = checkBuiltinType,
        [astAutoType] = checkBuiltinType,
        [astVoidType] = checkBuiltinType,
        [astPointerType] = checkPointerType,
        [astTupleType] = checkTupleType,
        [astFuncType] = checkFunctionType,
        [astArrayType] = checkArrayType,
        [astOptionalType] = checkOptionalType,
        [astDefine] = checkDefine,
        [astIdentifier] = checkIdentifier,
        [astPath] = checkPath,
        [astFuncDecl] = checkFunctionDecl,
        [astFuncParam] = checkFunctionParam,
        [astVarDecl] = checkVarDecl,
        [astTypeDecl] = checkTypeDecl,
        [astUnionDecl] = checkUnionDecl,
        [astEnumDecl] = checkEnumDecl,
        [astGenericDecl] = checkGenericDecl,
        [astField] = checkField,
        [astStructDecl] = checkStructDecl,
        [astClassDecl] = checkClassDecl,
        [astInterfaceDecl] = checkInterfaceDecl,
        [astImportDecl] = checkImportDecl,
        [astReturnStmt] = checkReturnStmt,
        [astBlockStmt] = checkBlockStmt,
        [astDeferStmt] = checkDeferStmt,
        [astBreakStmt] = checkBreakOrContinueStmt,
        [astContinueStmt] = checkBreakOrContinueStmt,
        [astIfStmt] = checkIfStmt,
        [astWhileStmt] = checkWhileStmt,
        [astForStmt] = checkForStmt,
        [astExprStmt] = checkExprStmt,
        [astCaseStmt] = checkCaseStmt,
        [astSwitchStmt] = checkSwitchStmt,
        [astMatchStmt] = checkMatchStmt,
        [astStringExpr] = checkStringExpr,
        [astCallExpr] = checkCallExpr,
        [astTupleExpr] = checkTupleExpr,
        [astMemberExpr] = checkMemberExpr,
        [astSpreadExpr] = checkSpreadExpr,
        [astBinaryExpr] = checkBinaryExpr,
        [astUnaryExpr] = checkUnaryExpr,
        [astAddressOf] = checkAddressOfExpr,
        [astAssignExpr] = checkAssignExpr,
        [astIndexExpr] = checkIndexExpr,
        [astStructExpr] = checkStructExpr,
        [astNewExpr] = checkNewExpr,
        [astCastExpr] = checkCastExpr,
        [astTypedExpr] = checkTypedExpr,
        [astStmtExpr] = checkStmtExpr,
        [astGroupExpr] = checkGroupExpr,
        [astClosureExpr] = checkClosureExpr,
        [astArrayExpr] = checkArrayExpr,
        [astMacroCallExpr] = checkMacroCallExpr,
        [astTernaryExpr] = checkTernaryExpr
    }, .fallback = astVisitFallbackVisitAll, .dispatch = withSavedStack);
    // clang-format on

    EvalContext evalContext = {.L = driver->L,
                               .pool = &driver->pool,
                               .strings = &driver->strPool,
                               .types = driver->typeTable,
                               .typer = &visitor};
    AstVisitor evaluator;
    initEvalVisitor(&evaluator, &evalContext);
    context.evaluator = &evaluator;

    context.types->currentNamespace =
        node->program.module ? node->program.module->moduleDecl.name : NULL;

    astVisit(&visitor, node);

    context.types->currentNamespace = NULL;

    return node;
}
