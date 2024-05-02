//
// Created by Carter on 2023-09-01.
//

#pragma once

#include <lang/frontend/ast.h>

#ifdef __cplusplus
extern "C" {
#endif

bool isBuiltinsInitialized();
void initializeBuiltins(Log *L, const FileLoc *loc, const Type *module);
AstNode *findBuiltinDecl(cstring name);
const Type *findBuiltinType(cstring name);
bool isBuiltinString(const Type *type);

#ifdef __cplusplus
}
#endif
