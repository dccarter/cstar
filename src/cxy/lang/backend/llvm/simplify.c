//
// Created by Carter Mbotho on 2024-01-10.
//

#include "llvm.h"

#include "driver/driver.h"
#include "lang/frontend/ast.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/visitor.h"

typedef struct SimplifyContext {
    Log *L;
    TypeTable *types;
    StrPool *strings;
    MemPool *pool;
    struct {
        AstNode *program;
        AstNode *previous;
        AstNode *current;
    } root;
    struct {
        struct {
            AstNode *currentFunction;
        };
        struct {
            AstNode *currentFunction;
        } stack;
    };
} SimplifyContext;

static void addTopLevelDecl(SimplifyContext *ctx, AstNode *node)
{
    csAssert0(ctx->root.current);

    node->next = ctx->root.current;
    if (ctx->root.previous)
        ctx->root.previous->next = node;
    else
        ctx->root.program->program.decls = node;
    ctx->root.previous = node;
}

static void addTopLevelDeclarationAsNext(SimplifyContext *ctx, AstNode *node)
{
    csAssert0(ctx->root.current);
    node->next = ctx->root.current->next;
    ctx->root.current->next = node;
}

static void visitProgram(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *decl = node->program.decls;
    ctx->root.program = node;
    astVisit(visitor, node->program.module);
    astVisitManyNodes(visitor, node->program.top);

    for (; decl; decl = decl->next) {
        ctx->root.previous = ctx->root.current;
        ctx->root.current = decl;
        astVisit(visitor, decl);
    }
}

static void visitCallExpr(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *callee = node->callExpr.callee, *args = node->callExpr.args;
    AstNode *func = callee->type->func.decl;
    csAssert0(func);
    if (func->funcDecl.this_ == NULL)
        return;
    astVisit(visitor, callee);
    csAssert0(nodeIs(callee, MemberExpr));
    AstNode *target = callee->memberExpr.target,
            *call = callee->memberExpr.member;

    csAssert0(call->type == callee->type);

    if (!typeIs(target->type, Pointer)) {
        target = makeAddrOffExpr(ctx->pool,
                                 &target->loc,
                                 func->funcDecl.this_->flags,
                                 target,
                                 NULL,
                                 func->funcDecl.this_->type);
    }
    target->next = args;
    node->callExpr.args = target;
    node->callExpr.callee = call;
}

static void visitPathElement(AstVisitor *visitor, AstNode *node)
{
    AstNode copy = *node;
    node->tag = astIdentifier;
    node->ident.value = copy.pathElement.name;
    node->ident.resolvesTo = copy.pathElement.resolvesTo;
    node->ident.super = copy.pathElement.super;
}

static void visitPathExpr(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    if (hasFlag(node, AddThis)) {
        csAssert0(ctx->currentFunction && ctx->currentFunction->funcDecl.this_);
        AstNode *this = ctx->currentFunction->funcDecl.this_;
        node->path.elements = makeResolvedPath(ctx->pool,
                                               &node->loc,
                                               S_this,
                                               flgNone,
                                               this,
                                               node->path.elements,
                                               this->type);
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
    AstNodeList fields = {NULL};
    u64 i = 0;

    AstNode *member = node->structDecl.members, *next = member;
    for (; next;) {
        member = next;
        next = next->next;
        member->next = NULL;
        if (nodeIs(member, Field)) {
            member->fieldExpr.index = i++;
            insertAstNode(&fields, member);
            continue;
        }

        if (nodeIs(member, FuncDecl) && member->funcDecl.this_)
            member->funcDecl.signature->params = member->funcDecl.this_;
        addTopLevelDeclarationAsNext(ctx, member);
    }
    node->structDecl.members = fields.first;
}

static void visitFuncDecl(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    ctx->currentFunction = node;
    astVisitFallbackVisitAll(visitor, node);
    ctx->currentFunction = NULL;
}

void simplifyAst(CompilerDriver *driver, AstNode *node)
{
    SimplifyContext context = {.L = driver->L,
                               .types = driver->typeTable,
                               .strings = &driver->strPool,
                               .pool = &driver->pool};

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context, {
        [astProgram] = visitProgram,
        [astPath] = visitPathExpr,
        [astPathElem] = visitPathElement,
        [astCallExpr] = visitCallExpr,
        [astStructDecl] = visitStructDecl,
        [astFuncDecl] = visitFuncDecl,
        [astGenericDecl] = astVisitSkip
    }, .fallback = astVisitFallbackVisitAll);
    // clang-format on

    astVisit(&visitor, node);
}
