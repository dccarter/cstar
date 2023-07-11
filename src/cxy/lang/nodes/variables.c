//
// Created by Carter on 2023-04-18.
//

#include "lang/codegen.h"
#include "lang/eval.h"
#include "lang/semantics.h"

#include "lang/flag.h"
#include "lang/ttable.h"
#include "lang/visitor.h"

static inline bool isTransientVariable(const AstNode *node)
{
    return findAttribute(node, "transient");
}

void generateVariableDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    bool isTransient = findAttribute(node, "transient");
    if (!isTransient && !hasFlag(node, ImmediatelyReturned) &&
        typeIs(node->type, Pointer))
        format(ctx->state, "__cxy_stack_cleanup ", NULL);

    if (hasFlag(node, Native))
        format(ctx->state, "extern ", NULL);

    if (hasFlag(node, Const) && !hasFlag(node->type, Const))
        format(ctx->state, "const ", NULL);

    generateTypeUsage(ctx, node->type);

    format(ctx->state, " ", NULL);
    if (hasFlag(node, TopLevelDecl))
        writeNamespace(ctx, "__");
    astConstVisit(visitor, node->varDecl.names);

    if (node->varDecl.init) {
        format(ctx->state, " = ", NULL);
        if (!isTransient && typeIs(node->varDecl.init->type, Pointer) &&
            !nodeIs(node->varDecl.init, NewExpr) &&
            !(nodeIs(node->varDecl.init, StmtExpr))) {
            format(ctx->state, "__builtin_get_ref(", NULL);
            astConstVisit(visitor, node->varDecl.init);
            format(ctx->state, ")", NULL);
        }
        else
            astConstVisit(visitor, node->varDecl.init);
    }
    format(ctx->state, ";", NULL);
}

void checkVarDecl(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *names = node->varDecl.names;

    if (node->varDecl.names->next) {
        logError(
            ctx->L,
            &node->loc,
            "unsupported: multi-variable declaration currently not supported",
            NULL);

        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (hasFlag(node->varDecl.names, Comptime)) {
        if (!evaluate(visitor, node->varDecl.names)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }

        if (!nodeIs(node->varDecl.names, Identifier)) {
            logError(ctx->L,
                     &node->varDecl.names->loc,
                     "comptime computed variable must resolve to an identifier",
                     NULL);

            node->type = ERROR_TYPE(ctx);
            return;
        }
    }

    if (node->varDecl.type) {
        node->varDecl.type->flags |= node->flags;
        node->type = evalType(visitor, node->varDecl.type);
    }
    else {
        node->type = makeAutoType(ctx->typeTable);
    }

    const Type *value = NULL;
    if (node->varDecl.init) {
        value = evalType(visitor, node->varDecl.init);
        if (typeIs(value, Error)) {
            node->type = ERROR_TYPE(ctx);
        }
        else if (typeIs(value, Array) && !isSliceType(value) &&
                 !nodeIs(node->varDecl.init, ArrayExpr)) {
            logError(ctx->L,
                     &node->varDecl.init->loc,
                     "initializer for array declaration can only be an array "
                     "expression",
                     NULL);
            node->type = ERROR_TYPE(ctx);
        }
        else if (!isTypeAssignableFrom(node->type, value)) {
            logError(ctx->L,
                     &node->varDecl.init->loc,
                     "incompatible types, expecting type '{t}', got '{t}'",
                     (FormatArg[]){{.t = node->type}, {.t = value}});
            node->type = ERROR_TYPE(ctx);
        }
        else if ((value->tag == typPointer) &&
                 ((value->flags & flgConst) && !(node->flags & flgConst))) {
            logError(ctx->L,
                     &node->varDecl.init->loc,
                     "assigning a const pointer to a non-const variable "
                     "discards const qualifier",
                     NULL);
            node->type = ERROR_TYPE(ctx);
        }

        if (node->type->tag == typAuto) {
            if (hasFlag(node, Const) && !hasFlag(value, Const))
                node->type = makeWrappedType(ctx->typeTable, value, flgConst);
            else
                node->type = value;
        }
    }

    if (typeIs(node->type, Error))
        return;

    if (!defineDeclaration(ctx,
                           names->ident.value,
                           getDeclarationAlias(ctx, node),
                           node,
                           hasFlag(node, Public))) //
    {
        node->type = ERROR_TYPE(ctx);
    }
}

void evalVarDecl(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    AstNode *names = node->varDecl.names;

    if (names->next) {
        logError(ctx->L,
                 &node->loc,
                 "unsupported: compile time multi-variable declaration not "
                 "supported",
                 NULL);

        node->tag = astError;
        return;
    }

    if (!evaluate(visitor, node->varDecl.init)) {
        node->tag = astError;
        return;
    }

    updateSymbol(&ctx->eval.env, names->ident.value, node->varDecl.init);
    node->flags = flgVisited;
    node->tag = astNop;
}
