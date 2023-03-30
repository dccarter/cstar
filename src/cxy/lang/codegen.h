//
// Created by Carter on 2023-03-29.
//

#pragma once

#include <lang/ast.h>

typedef struct {
    FormatState *state;
} CodeGenContext;

void generateCode(TypeTable *table, const AstNode *prog);

void generateManyAsts(ConstAstVisitor *visitor,
                      const char *sep,
                      const AstNode *nodes);

void generateManyAstsWithDelim(ConstAstVisitor *visitor,
                               const char *open,
                               const char *sep,
                               const char *close,
                               const AstNode *nodes);

void generateAstWithDelim(ConstAstVisitor *visitor,
                          const char *open,
                          const char *close,
                          const AstNode *node);

void generateManyAstsWithinBlock(ConstAstVisitor *visitor,
                                 const char *sep,
                                 const AstNode *nodes,
                                 bool newLine);
