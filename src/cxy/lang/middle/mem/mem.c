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

BlockScope *vtPushBlockScope(BlockScopeContainer *bsc, AstNode *node)
{
    BlockScope *prev = bsc->scope;
    BlockScope *scope = blockScopeContainerPush(bsc, node, flgNone);
    if (prev) {
        // Copy trace info from previous variable decls
        dynArrayFor(var, VariableTrace, &prev->data)
        {
            u32 idx = scope->data.size;
            pushOnDynArray(&scope->data,
                           &(VariableTrace){.scope = var->scope,
                                            .variable = var->variable,
                                            .prev = var->variable->_idx,
                                            .state = var->state});
            var->variable->_idx = idx;
        }
    }
    return scope;
}

void vtPopBlockScope(BlockScopeContainer *bsc)
{
    BlockScope *scope = bsc->scope;
    dynArrayFor(var, VariableTrace, &scope->data)
    {
        var->variable->_idx = var->prev;
    }
    blockScopeContainerPop(bsc);
}

u32 vtCreate(BlockScope *scope, AstNode *var)
{
    pushOnDynArray(
        &scope->data,
        &(VariableTrace){
            .scope = scope, .variable = var, .prev = 0, .state = vtsUnsigned});
    return scope->data.size - 1;
}

VariableTrace *vtGet(BlockScope *scope, u32 idx)
{
    csAssert0(idx < scope->data.size);
    return &dynArrayAt(VariableTrace *, &scope->data, idx);
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

    checkReferenceVariables(&context, node);
    if (hasErrors(driver->L))
        return NULL;

    manageMemory(&context, node);
    if (hasErrors(driver->L))
        return NULL;

    node = simplifyDeferStatements(driver, node);
    if (hasErrors(driver->L))
        return NULL;

    return node;
}