//
// Created by Carter Mbotho on 2024-07-17.
//

#include "mem.h"
#include "../defer.h"

#include "lang/frontend/ast.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/types.h"
#include "lang/frontend/visitor.h"

#include "core/alloc.h"
#include "lang/middle/scope.h"

#include "v2/mem.h"

const AstNode *getCallExprCalleeFunc(const AstNode *node)
{
    const AstNode *caller = resolveCallExpr(node);
    return caller ? getTypeDecl(caller->callExpr.callee->type) : NULL;
}

bool isReferenceCallExpr(const AstNode *node)
{
    if (!node || !isClassType(node->type) && !hasReferenceMembers(node->type))
        return false;
    const AstNode *caller = resolveCallExpr(node);
    if (caller == NULL)
        return false;
    const AstNode *decl = getTypeDecl(caller->callExpr.callee->type);
    return hasFlag(decl, Reference);
}

void withSavedStack(Visitor func, AstVisitor *visitor, AstNode *node)
{
    MMContext *ctx = getAstVisitorContext(visitor);
    __typeof(ctx->stack) stack = ctx->stack;
    node->parentScope = node->parentScope ?: ctx->current;
    ctx->current = node;
    func(visitor, node);

    ctx->stack = stack;
}

AstNode *memoryManageAst(CompilerDriver *driver, AstNode *node)
{
    MMContext context = {.L = driver->L,
                         .pool = driver->pool,
                         .strings = driver->strings,
                         .types = driver->types,
                         .program = node,
                         .traceMemory = driver->options.withMemoryTrace &
                                        isBuiltinsInitialized()};

    if (!isBuiltinsInitialized())
        return node;

    manageMemoryV2(driver, node);
    if (hasErrors(driver->L))
        return NULL;

    memoryFinalize(driver, node);
    if (hasErrors(driver->L))
        return NULL;

    return node;
}