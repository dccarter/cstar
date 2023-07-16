//
// Created by Carter Mbotho on 2023-07-14.
//

#include "lang/operations.h"

#include "lang/flag.h"
#include "lang/ttable.h"
#include "lang/types.h"
#include "lang/visitor.h"

#define ERROR_TYPE(CTX) makeErrorType((CTX)->types)

typedef struct {
    Log *L;
    MemPool *pool;
    StrPool *strings;
    TypeTable *types;
} TypingContext;

static Type *resolveGenericType(AstVisitor *visitor,
                                const Type *generic,
                                AstNode *node)
{
}

static Type *evalType(AstVisitor *visitor, AstNode *node) {}

void checkLiteral(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    switch (node->tag) {
    case astNullLit:
        node->type = makeVoidPointerType(ctx->types, flgNone);
        break;
    astBoolLit:
        node->type = getPrimitiveType(ctx->types, prtBool);
        break;
    astCharLit:
        node->type = getPrimitiveType(ctx->types, prtChar);
        break;
    astIntegerLit:
        node->type = getPrimitiveType(ctx->types, prtI64);
        break;
    astFloatLit:
        node->type = getPrimitiveType(ctx->types, prtF64);
        break;
    astStringLit:
        node->type = makeStringType(ctx->types);
        break;
    default:
        unreachable("NOT LITERAL");
    }
}

void checkIdentifier(AstVisitor *visitor, AstNode *node)
{
    csAssert0(node->ident.resolvesTo);
    node->type = node->ident.resolvesTo->type;
}

const Type *checkPathElement(AstVisitor *visitor,
                             const Type *parent,
                             AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const AstNode *resolved = NULL;
    switch (parent->tag) {
    case typStruct:
    case typContainer:
    case typModule:
    case typEnum:
        resolved = expectSymbolInType(
            parent, ctx->L, node->pathElement.name, &node->loc);
        break;
    default:
        logError(ctx->L,
                 &node->loc,
                 "expression of type '{t}' does not support member expressions",
                 (FormatArg[]){{.t = parent}});
        break;
    }

    if (resolved == NULL || typeIs(resolved->type, Error))
        return node->type = ERROR_TYPE(ctx);

    node->type = resolved->type;
    node->flags = resolved->flags;
    return node->type;
}

void checkPath(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *base = node->path.elements, *elem = base->next;
    const Type *type = base->pathElement.resolvesTo->type;

    csAssert0(base->pathElement.resolvesTo);

    if (typeIs(type, Generic))
        type = resolveGenericType(visitor, type, base);

    for (; elem; elem = elem->next) {
        type = checkPathElement(visitor, stripAll(type), elem);
        if (typeIs(type, Error)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }
    }
    node->type = type;
}
