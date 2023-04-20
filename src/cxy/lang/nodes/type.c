//
// Created by Carter on 2023-04-18.
//

#include "lang/codegen.h"
#include "lang/semantics.h"

#include "lang/ttable.h"

#include "core/alloc.h"

void generateTypeDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (!(node->flags & flgNative))
        generateTypeUsage(ctx, node->type);
}

void generateTypeinfo(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    csAssert0((hasFlag(node, Typeinfo)));
    writeTypename(ctx, node->type->info.target);
}

void checkTypeDecl(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    defineSymbol(&ctx->env, ctx->L, node->typeDecl.name, node);
    if (node->typeDecl.aliased) {
        const Type *ref = evalType(visitor, node->typeDecl.aliased);
        node->type = makeAliasType(ctx->typeTable, ref, node->typeDecl.name);
    }
    else {
        node->type = makeOpaqueType(ctx->typeTable, node->typeDecl.name);
    }
}

void checkUnionDecl(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    defineSymbol(&ctx->env, ctx->L, node->unionDecl.name, node);

    u64 count = countAstNodes(node->unionDecl.members);
    const Type **members = mallocOrDie(sizeof(Type *) * count);

    AstNode *member = node->unionDecl.members;
    for (u64 i = 0; member; member = member->next, i++) {
        members[i] = evalType(visitor, member);
        if (members[i] == ERROR_TYPE(ctx))
            node->type = ERROR_TYPE(ctx);
    }

    if (node->type == NULL)
        node->type = makeUnionType(ctx->typeTable, members, count);

    free((void *)members);
}

void checkBuiltinType(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    switch (node->tag) {
    case astVoidType:
        node->type = makeVoidType(ctx->typeTable);
        break;
    case astStringType:
        node->type = makeStringType(ctx->typeTable);
        break;
    default:
        logError(ctx->L, &node->loc, "unsupported builtin type", NULL);
        node->type = ERROR_TYPE(ctx);
        break;
    }
}

void checkOptionalType(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *type = evalType(visitor, node->optionalType.type);
    node->type = makeOptionalType(ctx->typeTable, type, flgNone);
}

void checkPrimitiveType(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    node->type = getPrimitiveType(ctx->typeTable, node->primitiveType.id);
}

void checkPointerType(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    node->type = makePointerType(ctx->typeTable,
                                 evalType(visitor, node->pointerType.pointed),
                                 node->flags & flgConst);
}
