//
// Created by Carter on 2023-08-30.
//

#include "eval.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"

f64 nodeGetNumericLiteral(const AstNode *node)
{
    switch (node->tag) {
    case astNullLit:
        return 0;
    case astBoolLit:
        return node->boolLiteral.value;
    case astCharLit:
        return node->charLiteral.value;
    case astIntegerLit:
        return (f64)integerLiteralValue(node);
    case astFloatLit:
        return node->floatLiteral.value;
    default:
        unreachable("NOT A LITERAL");
    }
}

i64 getEnumLiteralValue(const AstNode *node)
{
    csAssert0(typeIs(node->type, Enum));
    cstring name = NULL;
    if (nodeIs(node, Path) && node->path.elements->next)
        name = node->path.elements->next->pathElement.name;
    else if (nodeIs(node, MemberExpr) &&
             nodeIs(node->memberExpr.member, Identifier))
        name = node->memberExpr.member->ident.value;
    else
        unreachable("UNSUPPORTED");

    const EnumOptionDecl *option = findEnumOption(node->type, name);
    csAssert0(option);

    return option->value;
}

void nodeSetNumericLiteral(AstNode *node, AstNode *lhs, AstNode *rhs, f64 value)
{
    switch (lhs->tag) {
    case astBoolLit:
        if (nodeIs(rhs, BoolLit)) {
            node->tag = astBoolLit;
            node->boolLiteral.value = value == 0;
        }
        else if (nodeIs(rhs, CharLit) || nodeIs(rhs, IntegerLit)) {
            node->tag = astIntegerLit;
            node->intLiteral.isNegative = value < 0;
            if (node->intLiteral.isNegative)
                node->intLiteral.value = (i64)value;
            else
                node->intLiteral.uValue = (u64)value;
        }
        else {
            node->tag = astFloatLit;
            node->floatLiteral.value = value;
        }
        break;
    case astCharLit:
        if (nodeIs(rhs, BoolLit) || nodeIs(rhs, CharLit)) {
            node->tag = astCharLit;
            node->charLiteral.value = (wchar)value;
        }
        else if (nodeIs(rhs, IntegerLit)) {
            node->tag = astIntegerLit;
            node->intLiteral.isNegative = value < 0;
            if (node->intLiteral.isNegative)
                node->intLiteral.value = (i64)value;
            else
                node->intLiteral.uValue = (u64)value;
        }
        else {
            node->tag = astFloatLit;
            node->floatLiteral.value = value;
        }
        break;
    case astIntegerLit:
        if (nodeIs(rhs, FloatLit)) {
            node->tag = astFloatLit;
            node->floatLiteral.value = value;
        }
        else {
            node->tag = astIntegerLit;
            node->intLiteral.value = (i64)value;
            if (node->intLiteral.isNegative)
                node->intLiteral.value = (i64)value;
            else
                node->intLiteral.uValue = (u64)value;
        }
        break;
    case astFloatLit:
        node->tag = astFloatLit;
        node->floatLiteral.value = value;
        break;
    default:
        unreachable("NOT SUPPORTED");
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
        node->boolLiteral.value = integerLiteralValue(node) != 0;
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
