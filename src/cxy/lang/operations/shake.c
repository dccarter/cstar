//
// Created by Carter on 2023-07-10.
//

#include "lang/operations.h"
#include "lang/semantics.h"

AstNode *shakeAstNode(CompilerDriver *driver, AstNode *node)
{
    semanticsCheck(node,
                   driver->L,
                   &driver->pool,
                   &driver->strPool,
                   driver->typeTable,
                   driver->builtins);

    return node;
}
