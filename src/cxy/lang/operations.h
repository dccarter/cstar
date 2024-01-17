//
// Created by Carter on 2023-07-06.
//

#pragma once

#include "driver/driver.h"
#include "lang/frontend/ast.h"

typedef struct cJSON cJSON;

AstNode *shakeAstNode(CompilerDriver *driver, AstNode *node);
AstNode *dumpAstJson(CompilerDriver *driver, AstNode *node, FILE *file);
AstNode *dumpAstToYaml(CompilerDriver *driver, AstNode *node, FILE *file);
AstNode *bindAst(CompilerDriver *driver, AstNode *node);
AstNode *checkAst(CompilerDriver *driver, AstNode *node);
AstNode *finalizeAst(CompilerDriver *driver, AstNode *node);
AstNode *generateCode(CompilerDriver *driver, AstNode *node);
AstNode *collectAst(CompilerDriver *driver, AstNode *node);