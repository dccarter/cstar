//
// Created by Carter on 2023-07-06.
//

#pragma once

#include <driver/driver.h>
#include <lang/ast.h>

AstNode *shakeAstNode(CompilerDriver *driver, AstNode *node);
AstNode *dumpAst(CompilerDriver *driver, AstNode *node);