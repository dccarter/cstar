//
// Created by Carter on 2023-04-18.
//

#include "lang/codegen.h"
#include "lang/semantics.h"

#include "lang/ttable.h"

void generateVariableDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);

    if (node->flags & flgNative)
        format(ctx->state, "extern ", NULL);

    if ((node->flags & flgConst) && !(node->type->flags & flgConst))
        format(ctx->state, "const ", NULL);

    generateTypeUsage(ctx, node->type);

    format(ctx->state, " ", NULL);
    if (node->flags & flgTopLevelDecl)
        writeNamespace(ctx, "__");
    astConstVisit(visitor, node->varDecl.names);

    if (node->varDecl.init) {
        format(ctx->state, " = ", NULL);
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

    defineSymbol(ctx->env, ctx->L, names->ident.value, node);
    addModuleExport(ctx, node, names->ident.value);

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
        if (node->type->tag == typAuto)
            node->type = value;
    }
}
