//
// Created by Carter on 2023-04-26.
//

#pragma once

#include <lang/scope.h>

typedef struct {
    Env *env;
    AstNode *result;
} EvaluatorContext;

AstNode *evaluate(AstVisitor *visitor, AstNode *node);
