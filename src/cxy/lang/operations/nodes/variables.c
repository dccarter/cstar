//
// Created by Carter on 2023-09-13.
//
#include "../check.h"
#include "../codegen.h"
#include "../eval.h"

#include "lang/flag.h"
#include "lang/operations.h"
#include "lang/ttable.h"
#include "lang/visitor.h"

#include "core/alloc.h"

bool isLValueExpr(const AstNode *node)
{
    switch (node->tag) {
    case astPath:
    case astMemberExpr:
    case astIndexExpr:
        return true;
    default:
        return false;
    }
}

void generateExpressionWithMemoryManagement(ConstAstVisitor *visitor,
                                            const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (!isLValueExpr(node) || !hasReferenceMembers(node->type) ||
        hasFlag(node, TopLevelDecl)) //
    {
        astConstVisit(visitor, node);
        return;
    }

    if (isClassType(node->type)) {
        format(ctx->state, "(", NULL);
        generateTypeUsage(ctx, node->type);
        format(ctx->state, ")CXY__default_get_ref(", NULL);
        astConstVisit(visitor, node);
        format(ctx->state, ")", NULL);
    }
    else if (isStructType(node->type) || isTupleType(node->type)) {
        generateTypeUsage(ctx, node->type);
        format(ctx->state, "__op__copy(&(", NULL);
        astConstVisit(visitor, node);
        format(ctx->state, "))", NULL);
    }
    else {
        astConstVisit(visitor, node);
    }
}

void generateCleanupAttribute(CodegenContext *ctx, const AstNode *node)
{
    const Type *type = node->type;
    if (hasFlag(node, ImmediatelyReturned) || hasFlag(node->type, Closure))
        return;

    if (isClassType(type)) {
        format(ctx->state, "CXY__stack_cleanup ", NULL);
    }
    else if (hasReferenceMembers(node->type) &&
             (isStructType(type) || isTupleType(type))) //
    {
        format(ctx->state, "__attribute__((cleanup(", NULL);
        generateTypeUsage(ctx, type);
        format(ctx->state, "__op__destructor_fwd))) ", NULL);
    }
}

void generateVariableDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);

    if (hasFlag(node, Native))
        format(ctx->state, "extern ", NULL);
    else
        generateCleanupAttribute(ctx, node);

    if (hasFlag(node, Const) && !hasFlag(node->type, Const))
        format(ctx->state, "const ", NULL);

    generateTypeUsage(ctx, node->type);

    format(ctx->state, " ", NULL);
    if (hasFlag(node, TopLevelDecl))
        writeNamespace(ctx, "__");
    astConstVisit(visitor, node->varDecl.names);

    if (node->varDecl.init) {
        format(ctx->state, " = ", NULL);
        generateExpressionWithMemoryManagement(visitor, node->varDecl.init);
    }
    format(ctx->state, ";", NULL);
}

void checkVarDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *type = node->varDecl.type, *init = node->varDecl.init,
            *name = node->varDecl.names;
    if (hasFlag(name, Comptime) && !evaluate(ctx->evaluator, name)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    const Type *type_ =
        type ? checkType(visitor, type) : makeAutoType(ctx->types);
    const Type *init_ = checkType(visitor, init);

    if (isSliceType(type_) && nodeIs(init, ArrayExpr)) {
        AstNode *newVar = transformArrayExprToSliceCall(ctx, type_, init);
        init_ = checkType(visitor, newVar);
        if (typeIs(init_, Error)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }

        addBlockLevelDeclaration(ctx, newVar);
        node->varDecl.init = makeSliceConstructor(ctx, type_, newVar);
        type_ = checkType(visitor, node);
        if (typeIs(type_, Error))
            node->type = ERROR_TYPE(ctx);
        return;
    }

    if (typeIs(type_, Error) || typeIs(init_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (init_ == NULL) {
        node->type = type_;
        return;
    }

    if (!isTypeAssignableFrom(type_, init_)) {
        logError(ctx->L,
                 &node->loc,
                 "variable initializer of type '{t}' is not assignable to "
                 "variable type '{t}",
                 (FormatArg[]){{.t = init_}, {.t = type_}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    node->type = typeIs(type_, Auto) ? init_ : type_;
    if (hasFlag(node, Const) && !hasFlag(node->type, Const)) {
        node->type = makeWrappedType(ctx->types, node->type, flgConst);
    }

    if (!hasFlag(type_, Optional) || hasFlag(init_, Optional))
        return;

    const Type *target = getOptionalTargetType(type_);
    if (nodeIs(init, NullLit)) {
        if (!transformOptionalNone(visitor, init, target)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }
    }
    else {
        init->type = target;
        if (!transformOptionalSome(
                visitor, init, copyAstNode(ctx->pool, init))) //
        {
            node->type = ERROR_TYPE(ctx);
            return;
        }
    }
}

void evalVarDecl(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
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

    // retain comptime
    node->flags = flgVisited | flgComptime;
}
