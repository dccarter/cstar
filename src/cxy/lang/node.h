//
// Created by Carter on 2023-05-09.
//

#pragma once

#include <core/utils.h>

#include <lang/ast.h>

typedef struct SemanticsContext SemanticsContext;
typedef struct AstVisitor AstVisitor;

AstNode *makeAddressOf(SemanticsContext *ctx, AstNode *node);
void transformToMemberCallExpr(AstVisitor *visitor,
                               AstNode *node,
                               AstNode *func,
                               AstNode *target,
                               cstring member,
                               AstNode *args);
