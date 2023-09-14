//
// Created by Carter on 2023-09-01.
//

#include "builtins.h"

static struct {
    const Type *module;
    FileLoc loc;
} cxy_builtins = {NULL};

bool isBuiltinsInitialized() { return cxy_builtins.module != NULL; }

void initializeBuiltins(Log *L, const FileLoc *loc, const Type *module)
{
    if (cxy_builtins.module != NULL) {
        logWarning(L,
                   &cxy_builtins.loc,
                   "initializing builtins should not allowed",
                   NULL);
        return;
    }

    cxy_builtins.module = module;
    cxy_builtins.loc = *loc;
}

AstNode *findBuiltinDecl(cstring name)
{
    csAssert0(cxy_builtins.module);
    const ModuleMember *member = findModuleMember(cxy_builtins.module, name);
    return member ? (AstNode *)member->decl : NULL;
}

const Type *findBuiltinType(cstring name)
{
    if (cxy_builtins.module) {
        const ModuleMember *member =
            findModuleMember(cxy_builtins.module, name);
        return member ? member->type : NULL;
    }
    return NULL;
}