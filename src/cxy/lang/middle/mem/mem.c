#include "mem.h"
#include "lang/middle/builtins.h"

AstNode *memoryManageAst(CompilerDriver *driver, AstNode *node)
{
    if (!isBuiltinsInitialized())
        return node;

    manageMemory(driver, node);
    if (hasErrors(driver->L))
        return NULL;

    memoryFinalize(driver, node);
    if (hasErrors(driver->L))
        return NULL;

    return node;
}