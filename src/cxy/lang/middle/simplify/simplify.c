//
// Created by Carter Mbotho on 2024-01-10.
//

#include "driver/driver.h"
#include "lang/frontend/ast.h"
#include "lang/frontend/capture.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/visitor.h"

typedef struct SimplifyContext {
    Log *L;
    TypeTable *types;
    StrPool *strings;
    MemPool *pool;
    AstModifier root;
    HashTable functions;
    struct {
        struct {
            AstNode *currentFunction;
            AstModifier block;
        };
        struct {
            AstNode *currentFunction;
            AstModifier block;
        } stack;
    };
} SimplifyContext;

static void generateForRangeStmt(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *range = node->forStmt.range, *var = node->forStmt.var,
            *body = node->forStmt.body;

    // var x = range.start
    var->varDecl.init = range->rangeExpr.start;

    // while (x < range.end) { ...; x += range.step; }
    var->next = makeWhileStmt(
        ctx->pool,
        &node->loc,
        node->flags,
        // x < range.end
        makeBinaryExpr(ctx->pool,
                       &range->loc,
                       flgNone,
                       // x
                       makeResolvedIdentifier(ctx->pool,
                                              &range->loc,
                                              var->varDecl.name,
                                              0,
                                              var,
                                              NULL,
                                              var->type),
                       opNe,
                       // range.end
                       range->rangeExpr.end,
                       NULL,
                       getPrimitiveType(ctx->types, prtBool)),
        // ...
        body,
        NULL,
        // x += range.step
        makeExprStmt(
            ctx->pool,
            &node->loc,
            flgNone,
            // x += range.step
            makeAssignExpr(
                ctx->pool,
                &range->loc,
                flgNone,
                // x
                makeResolvedIdentifier(ctx->pool,
                                       &range->loc,
                                       var->varDecl.name,
                                       0,
                                       var,
                                       NULL,
                                       var->type),
                opAdd,
                // range.step
                range->rangeExpr.step
                    ?: makeIntegerLiteral(
                           ctx->pool, &range->loc, 1, NULL, range->type),
                NULL,
                range->type),
            NULL,
            range->type));

    node->tag = astBlockStmt;
    node->blockStmt.stmts = var;
    node->blockStmt.last = var->next;
    astVisit(visitor, node);
}

static void generateForArrayStmt(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *range = node->forStmt.range, *var = node->forStmt.var,
            *body = node->forStmt.body;
    const Type *elem = typeIs(range->type, String)
                           ? getPrimitiveType(ctx->types, prtChar)
                           : range->type->array.elementType;

    AstNode *index =
        var->next
            ?: makeVarDecl(
                   ctx->pool,
                   &var->loc,
                   flgConst,
                   makeAnonymousVariable(ctx->strings, "i"),
                   NULL,
                   makeIntegerLiteral(ctx->pool,
                                      &var->loc,
                                      0,
                                      NULL,
                                      getPrimitiveType(ctx->types, prtU64)),
                   NULL,
                   getPrimitiveType(ctx->types, prtU64));
    if (var->next) {
        var->next->varDecl.init =
            makeIntegerLiteral(ctx->pool,
                               &var->loc,
                               0,
                               NULL,
                               getPrimitiveType(ctx->types, prtU64));
    }
    else {
        var->next = index;
    }

    range = makeVarDecl(ctx->pool,
                        &range->loc,
                        range->flags,
                        "",
                        NULL,
                        range,
                        NULL,
                        range->type);
    index->next = range;

    AstNode *condition = makeBinaryExpr(
        ctx->pool,
        &range->loc,
        flgNone,
        makeResolvedIdentifier(ctx->pool,
                               &range->loc,
                               index->varDecl.name,
                               0,
                               index,
                               NULL,
                               index->type),
        opLt,
        makeUnsignedIntegerLiteral(ctx->pool,
                                   &range->loc,
                                   range->type->array.len,
                                   NULL,
                                   getPrimitiveType(ctx->types, prtU64)),
        NULL,
        getPrimitiveType(ctx->types, prtBool));

    AstNode *assign = makeExprStmt(
        ctx->pool,
        &range->loc,
        flgNone,
        makeAssignExpr(ctx->pool,
                       &range->loc,
                       flgNone,
                       makeResolvedIdentifier(ctx->pool,
                                              &range->loc,
                                              var->varDecl.name,
                                              0,
                                              var,
                                              NULL,
                                              var->type),
                       opAssign,
                       makeIndexExpr(ctx->pool,
                                     &range->loc,
                                     elem->flags,
                                     makeResolvedIdentifier(ctx->pool,
                                                            &range->loc,
                                                            range->varDecl.name,
                                                            0,
                                                            range,
                                                            NULL,
                                                            range->type),
                                     makeResolvedIdentifier(ctx->pool,
                                                            &range->loc,
                                                            index->varDecl.name,
                                                            flgNone,
                                                            index,
                                                            NULL,
                                                            index->type),
                                     NULL,
                                     var->type),
                       NULL,
                       range->type),
        NULL,
        var->type);

    AstNode *advance = makeExprStmt(
        ctx->pool,
        &node->loc,
        flgNone,
        makeAssignExpr(
            ctx->pool,
            &range->loc,
            flgNone,
            makeResolvedIdentifier(ctx->pool,
                                   &range->loc,
                                   index->varDecl.name,
                                   0,
                                   index,
                                   NULL,
                                   index->type),
            opAdd,
            makeIntegerLiteral(ctx->pool, &range->loc, 1, NULL, index->type),
            NULL,
            index->type),
        NULL,
        index->type);
    assign->next = advance;

    if (nodeIs(body, BlockStmt)) {
        if (body->blockStmt.stmts) {
            assign->next = body->blockStmt.stmts;
            body->blockStmt.stmts = assign;
        }
        else
            body->blockStmt.stmts = assign;
    }
    else {
        assign->next = body;
        body = makeBlockStmt(ctx->pool, &assign->loc, assign, NULL, node->type);
    }

    range->next = makeWhileStmt(ctx->pool,
                                &node->loc,
                                node->flags,
                                condition,
                                body,
                                NULL,
                                deepCloneAstNode(ctx->pool, advance));
    node->tag = astBlockStmt;
    node->blockStmt.stmts = var;
    node->blockStmt.last = range->next;
    astVisit(visitor, node);
}

static bool addFunctionDecl(SimplifyContext *ctx, AstNode *decl)
{
    if (nodeIs(decl, FuncDecl)) {
        return insertInHashTable(&ctx->functions,
                                 &decl,
                                 hashPtr(hashInit(), decl),
                                 sizeof(void *),
                                 comparePointers);
    }
    return false;
}

static void visitProgram(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *decl = node->program.decls;
    astVisit(visitor, node->program.module);
    astVisitManyNodes(visitor, node->program.top);
    astModifierInit(&ctx->root, node);

    for (; decl; decl = decl->next) {
        astModifierNext(&ctx->root, decl);
        if (nodeIsNoop(decl)) {
            astModifierRemoveCurrent(&ctx->root);
            continue;
        }

        astVisit(visitor, decl);
    }
}

static void visitCallExpr(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *callee = node->callExpr.callee, *args = node->callExpr.args;
    AstNode *func = callee->type->func.decl;

    astVisit(visitor, callee);
    astVisitManyNodes(visitor, args);

    csAssert0(func);
    AstNode *this = getMemberFunctionThis(func);
    if (this == NULL || !nodeIs(callee, MemberExpr))
        return;

    csAssert0(nodeIs(callee, MemberExpr));
    AstNode *target = callee->memberExpr.target,
            *call = callee->memberExpr.member;

    call->type = callee->type;

    if (!typeIs(target->type, Pointer)) {
        target = makeAddrOffExpr(
            ctx->pool, &target->loc, this->flags, target, NULL, this->type);
    }
    astVisitManyNodes(visitor, args);
    target->next = args;
    node->callExpr.args = target;
    node->callExpr.callee = call;
}

static void visitIdentifier(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *target = node->ident.resolvesTo;
    if (nodeIs(target, FuncDecl)) {
        if (addFunctionDecl(ctx, target)) {
            // new function declaration added, add extern
            AstNode *decl = makeAstNode(ctx->pool,
                                        builtinLoc(),
                                        &(AstNode){.tag = astExternDecl,
                                                   .type = target->type,
                                                   .externDecl.func = target});

            astModifierAdd(&ctx->root, decl);
            node->ident.resolvesTo = decl;
        }
    }
}

static void visitPathElement(AstVisitor *visitor, AstNode *node)
{
    AstNode copy = *node;
    node->tag = astIdentifier;
    node->ident.value = copy.pathElement.name;
    node->ident.resolvesTo = copy.pathElement.resolvesTo;
    node->ident.super = copy.pathElement.super;
    astVisit(visitor, node);
}

static void visitPathExpr(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    if (hasFlag(node, AddThis)) {
        AstNode *this = getMemberFunctionThis(ctx->currentFunction);
        csAssert0(this);
        AstNode *base = node->path.elements;
        node->path.elements = makeResolvedPathElement(
            ctx->pool, &node->loc, S_this, flgNone, this, base, this->type);

        if (!hasFlag(node, Generated)) {
            // it already resolves to correct member
            base->pathElement.resolvesTo = findMemberDeclInType(
                stripAll(this->type), base->pathElement.name);
            csAssert0(base->pathElement.resolvesTo);
        }
        node->flags &= ~flgAddThis;
    }

    AstNode *elem = node->path.elements;
    astVisit(visitor, elem);
    if (elem->next == NULL) {
        replaceAstNodeWith(node, elem);
        return;
    }

    AstNode *target = elem, *next = elem->next;
    for (; next;) {
        elem = next;
        next = next->next;
        astVisit(visitor, elem);
        if (next) {
            target->next = NULL;
            elem->next = NULL;
            target = makeMemberExpr(ctx->pool,
                                    locExtend(&target->loc, &elem->loc),
                                    elem->flags,
                                    target,
                                    elem,
                                    NULL,
                                    elem->type);
        }
    }

    node->tag = astMemberExpr;
    node->memberExpr.target = target;
    node->memberExpr.member = elem;
}

static void visitStructDecl(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNodeList fields = {NULL}, others = {NULL};
    u64 i = 0;

    AstNode *member = node->structDecl.members, *next = member;
    for (; next;) {
        member = next;
        next = next->next;
        member->next = NULL;
        if (nodeIs(member, FieldDecl)) {
            member->fieldExpr.index = i++;
            insertAstNode(&fields, member);
            continue;
        }

        if (nodeIs(member, FuncDecl)) {
            if (member->funcDecl.this_)
                member->funcDecl.signature->params = member->funcDecl.this_;
            insertAstNode(&others, member);
        }
    }
    node->structDecl.members = fields.first;
    if (others.first)
        astModifierAddAsNext(&ctx->root, others.first);
}

void visitForStmt(AstVisitor *visitor, AstNode *node)
{
    AstNode *range = node->forStmt.range;
    if (nodeIs(range, RangeExpr))
        generateForRangeStmt(visitor, node);
    else
        generateForArrayStmt(visitor, node);
}

void visitBlockStmt(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    astModifierInit(&ctx->block, node);
    AstNode *stmt = node->blockStmt.stmts, *last = node->blockStmt.last;

    for (; stmt; stmt = stmt->next) {
        astModifierNext(&ctx->block, stmt);
        if (hasFlag(stmt, Comptime) || nodeIsNoop(stmt)) {
            astModifierRemoveCurrent(&ctx->block);
            continue;
        }
        astVisit(visitor, stmt);
        last = stmt;
    }

    node->blockStmt.last = last;
}

static void visitFuncDecl(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    ctx->currentFunction = node;
    addFunctionDecl(ctx, node);
    astVisitFallbackVisitAll(visitor, node);
    ctx->currentFunction = NULL;
}

AstNode *simplifyAst(CompilerDriver *driver, AstNode *node)
{
    SimplifyContext context = {.L = driver->L,
                               .types = driver->types,
                               .strings = driver->strings,
                               .pool = driver->pool,
                               .functions = newHashTable(sizeof(void *))};

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context, {
        [astProgram] = visitProgram,
        [astPath] = visitPathExpr,
        [astPathElem] = visitPathElement,
        [astIdentifier] = visitIdentifier,
        [astCallExpr] = visitCallExpr,
        [astForStmt] = visitForStmt,
        [astBlockStmt] = visitBlockStmt,
        [astStructDecl] = visitStructDecl,
        [astClassDecl] = visitStructDecl,
        [astFuncDecl] = visitFuncDecl,
        [astGenericDecl] = astVisitSkip,
    }, .fallback = astVisitFallbackVisitAll);
    // clang-format on

    astVisit(&visitor, node);
    freeHashTable(&context.functions);

    return node;
}
