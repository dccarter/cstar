//
// Created by Carter Mbotho on 2023-07-18.
//

#include "lang/ast.h"

AstNode *cloneGenericDeclaration(MemPool *pool, AstNode *node)
{
    AstNode *param = node->genericDecl.params;
    AstNode *decl = node->genericDecl.decl;
    AstNode *params = NULL, *it = NULL;
    CloneAstConfig config = {.pool = pool, .createMapping = true};

    initCloneAstNodeMapping(&config);

    for (; param; param = param->next) {
        AstNode *clone = cloneAstNode(&config, param);
        if (params == NULL)
            params = it = clone;
        else
            it = it->next = clone;
    }

    decl = cloneAstNode(&config, decl);
    if (nodeIs(decl, FuncDecl))
        decl->funcDecl.typeParams = params;
    else
        decl->structDecl.typeParams = params;

    deinitCloneAstNodeConfig(&config);

    return decl;
}
