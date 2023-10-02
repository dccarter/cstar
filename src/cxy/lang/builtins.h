//
// Created by Carter on 2023-09-01.
//

#pragma once

#include <lang/ast.h>

bool isBuiltinsInitialized();
void initializeBuiltins(Log *L, const FileLoc *loc, const Type *module);
AstNode *findBuiltinDecl(cstring name);
const Type *findBuiltinType(cstring name);
bool isBuiltinString(const Type *type);
