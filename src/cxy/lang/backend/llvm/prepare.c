//
// Created by Carter Mbotho on 2024-01-10.
//

#include "lang/frontend/visitor.h"

void promoteTypes(AstNode *lhs, AstNode *rhs)
{
    if (lhs->type == rhs->type)
        return;

    if (isBigger(rhs, lhs)) {
        // (RHSType) lhs
        promoteWithCast(lhs, rhs->type);
    }
    else {
        // (LHSType) rhs
        promoteWithCast(rhs, lhs->type);
    }
}

void explicitCast(AstNode *node, const Type *type) {}