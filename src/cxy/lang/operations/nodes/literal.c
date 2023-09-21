//
// Created by Carter on 2023-08-30.
//

#include "../check.h"
#include "../codegen.h"
#include "../eval.h"

#include "lang/operations.h"

#include "lang/flag.h"
#include "lang/strings.h"
#include "lang/ttable.h"
#include "lang/visitor.h"

#include <memory.h>

f64 getNumericLiteral(const AstNode *node)
{
    switch (node->tag) {
    case astNullLit:
        return 0;
    case astBoolLit:
        return node->boolLiteral.value;
    case astCharLit:
        return node->charLiteral.value;
    case astIntegerLit:
        return (f64)node->intLiteral.value;
    case astFloatLit:
        return node->floatLiteral.value;
    default:
        unreachable("NOT A LITERAL");
    }
}

void setNumericLiteralValue(AstNode *node,
                            AstNode *lhs,
                            AstNode *rhs,
                            f64 value)
{
    switch (lhs->tag) {
    case astBoolLit:
        if (nodeIs(rhs, BoolLit)) {
            node->tag = astBoolLit;
            node->boolLiteral.value = value == 0;
        }
        else if (nodeIs(rhs, CharLit) || nodeIs(rhs, IntegerLit)) {
            node->tag = astIntegerLit;
            node->intLiteral.value = (i64)value;
            node->intLiteral.hasMinus = value < 0;
        }
        else {
            node->tag = astFloatLit;
            node->intLiteral.value = (i64)value;
        }
        break;
    case astCharLit:
        if (nodeIs(rhs, BoolLit) || nodeIs(rhs, CharLit)) {
            node->tag = astCharLit;
            node->charLiteral.value = (wchar)value;
        }
        else if (nodeIs(rhs, IntegerLit)) {
            node->tag = astIntegerLit;
            node->intLiteral.value = (i64)value;
            node->intLiteral.hasMinus = value < 0;
        }
        else {
            node->tag = astFloatLit;
            node->intLiteral.value = (i64)value;
        }
        break;
    case astIntegerLit:
        if (nodeIs(rhs, FloatLit)) {
            node->tag = astFloatLit;
            node->intLiteral.value = (i64)value;
        }
        else {
            node->tag = astIntegerLit;
            node->intLiteral.value = (i64)value;
            node->intLiteral.hasMinus = value < 0;
        }
        break;
    case astFloatLit:
        node->tag = astFloatLit;
        node->intLiteral.value = (i64)value;
        break;
    default:
        unreachable("NOT SUPPORTED");
    }
}

void generateLiteral(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);

    switch (node->tag) {
    case astNullLit:
        format(ctx->state, "nullptr", NULL);
        break;
    case astBoolLit:
        format(
            ctx->state,
            "{s}",
            (FormatArg[]){{.s = node->boolLiteral.value ? "true" : "false"}});
        break;
    case astCharLit:
        format(ctx->state,
               "{u32}",
               (FormatArg[]){{.u32 = node->charLiteral.value}});
        break;
    case astIntegerLit:

        format(ctx->state,
               "{i64}",
               (FormatArg[]){{.i64 = integerLiteralValue(node)}});
        break;
    case astFloatLit:
        format(ctx->state,
               "{f64}",
               (FormatArg[]){{.f64 = node->floatLiteral.value}});
        break;
    case astStringLit:
        format(ctx->state,
               "\"{s}\"",
               (FormatArg[]){{.s = node->stringLiteral.value}});
        break;
    default:
        break;
    }
}

void checkLiteral(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    switch (node->tag) {
    case astNullLit:
        node->type = makeNullType(ctx->types);
        break;
    case astBoolLit:
        node->type = getPrimitiveType(ctx->types, prtBool);
        break;
    case astCharLit:
        node->type = getPrimitiveType(ctx->types, prtChar);
        break;
    case astIntegerLit:
        node->type =
            getIntegerTypeForLiteral(ctx->types, integerLiteralValue(node));
        break;
    case astFloatLit:
        node->type = getPrimitiveType(ctx->types, prtF64);
        break;
    case astStringLit:
        node->type = makeStringType(ctx->types);
        break;
    default:
        unreachable("NOT LITERAL");
    }
}

bool evalBooleanCast(EvalContext *ctx, AstNode *node)
{
    switch (node->tag) {
    case astBoolLit:
        break;
    case astNullLit:
        node->boolLiteral.value = false;
        break;
    case astCharLit:
        node->boolLiteral.value = node->charLiteral.value != '\0';
        node->tag = astBoolLit;
        break;
    case astIntegerLit:
        node->boolLiteral.value = node->intLiteral.value != 0;
        node->tag = astBoolLit;
        break;
    case astFloatLit:
        node->boolLiteral.value = node->floatLiteral.value != 0;
        node->tag = astBoolLit;
        break;
    case astStringLit:
        node->boolLiteral.value = (node->stringLiteral.value == NULL) ||
                                  (node->stringLiteral.value[0] != '\0');
        node->tag = astBoolLit;
        break;
    default:
        logError(ctx->L,
                 &node->loc,
                 "comp-time expression cannot be converted to boolean",
                 NULL);
        node->tag = astError;
        return false;
    }

    return true;
}
