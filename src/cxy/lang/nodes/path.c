//
// Created by Carter on 2023-04-18.
//

#include "lang/codegen.h"
#include "lang/semantics.h"

#include "lang/capture.h"
#include "lang/ttable.h"

#include "core/alloc.h"

static const Type *checkFirstPathElement(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    Scope *scope = NULL, *closure = ctx->closure;

    AstNode *symbol = findSymbolAndScope(
        &ctx->env, ctx->L, node->pathElement.name, &node->loc, &scope);
    u64 flags = flgNone;
    if (symbol == NULL) {
        node->type = ERROR_TYPE(ctx);
        return ERROR_TYPE(ctx);
    }

    if (scope->node && scope->node->tag == astStructDecl) {
        node->flags = flgAddThis;
        if (scope != ctx->env.first)
            node->flags |= flgAddSuper;
    }

    node->type = symbol->type;
    flags = (symbol->flags & (flgConst | flgAddThis));
    node->flags |= flags;
    if (closure == NULL)
        // We are outside a closure
        return node->type;
    if (isRootScope(scope) && isInSameEnv(closure, scope))
        // Symbol defined in global scope
        return node->type;
    if (!isAncestorScope(scope, closure))
        // Symbol not defined in parent scope of closure
        return node->type;

    node->pathElement.index = addClosureCapture(
        &closure->node->closureExpr.capture,
        node->pathElement.name,
        (node->type->tag == typPrimitive || node->type->tag == typPointer)
            ? node->type
            : makePointerType(ctx->typeTable,
                              node->type,
                              (flags & flgConst) | flgCapturePointer));

    node->flags |= flgCapture;

    return node->type;
}

void generatePathElement(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (node->flags & flgCapture) {
        if (node->type->tag == typPrimitive || node->type->tag == typPointer)
            format(ctx->state,
                   "self->_{u64}",
                   (FormatArg[]){{.u64 = node->pathElement.index}});
        else
            format(ctx->state,
                   "(*self->_{u64})",
                   (FormatArg[]){{.u64 = node->pathElement.index}});
    }
    else if (node->flags & flgAddThis) {
        format(ctx->state,
               "this->{s}{s}",
               (FormatArg[]){{.s = (node->flags & flgAddSuper) ? "super." : ""},
                             {.s = node->pathElement.name}});
    }
    else
        format(ctx->state, "{s}", (FormatArg[]){{.s = node->pathElement.name}});
}

void generatePath(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (node->type->tag == typEnum && node->path.elements->next &&
        (node->path.elements->next->flags & flgMember)) {
        writeEnumPrefix(ctx, node->type);
        generateManyAstsWithDelim(
            visitor, "_", "_", "", node->path.elements->next);
    }
    else if (node->type->tag == typFunc && node->type->func.decl->parentScope &&
             node->type->func.decl->parentScope->tag == astStructDecl) {
        const Type *scope = node->type->func.decl->parentScope->type;
        const AstNode *func = node->type->func.decl;
        writeTypename(ctx, scope);
        format(ctx->state, "__{s}", (FormatArg[]){{.s = func->funcDecl.name}});
    }
    else {
        const AstNode *elem = node->path.elements;
        for (; elem; elem = elem->next) {
            astConstVisit(visitor, elem);
            if (elem->next) {
                if (elem->type->tag == typPointer || elem->type->tag == typThis)
                    format(ctx->state, "->", NULL);
                else
                    format(ctx->state, ".", NULL);
            }
        }
    }
}

void checkPathElement(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    csAssert0(node->parentScope);
    const Type *scope = stripPointer(node->parentScope->type);

    const Env *env = NULL;
    Env thisEnv = {};
    switch (scope->tag) {
    case typEnum:
        env = scope->tEnum.env;
        break;
    case typStruct:
        env = scope->tStruct.env;
        break;
    case typThis:
        thisEnv = (Env){.first = ctx->env.first, .scope = ctx->env.first};
        env = &thisEnv;
        break;
    default:
        logError(ctx->L,
                 &node->loc,
                 "type '{t}' does not support member expressions",
                 (FormatArg[]){{.t = scope}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    AstNode *symbol =
        findSymbol(env, ctx->L, node->pathElement.name, &node->loc);

    if (symbol == NULL) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    node->flags = symbol->flags;
    node->type = symbol->type;
    switch (scope->tag) {
    case typEnum:
        if (node->parentScope->flags & flgMember) {
            logError(ctx->L,
                     &node->loc,
                     "member expression not supported on enum members",
                     NULL);
            node->type = ERROR_TYPE(ctx);
        }
        else {
            node->type = scope;
        }
        break;
    default:
        break;
    }
}

void checkPath(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    AstNode *elem = node->path.elements;
    const Type *type = checkFirstPathElement(visitor, elem);
    u64 flags = elem->flags;
    AstNode *prev = elem;
    elem = elem->next;
    for (; elem; elem = elem->next) {
        elem->parentScope = prev;
        if ((type = evalType(visitor, elem)) == ERROR_TYPE(ctx)) {
            node->type = ERROR_TYPE(ctx);
            return;
        };
        flags = elem->flags;
        prev = elem;
    }

    node->type = type;
    node->flags |= flags;
}
