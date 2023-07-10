/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-21
 */

#include "lang/semantics.h"

#include <string.h>

const AstNode *findAttribute(const AstNode *node, cstring name)
{
    const AstNode *attr = node->attrs;
    while (attr) {
        if (strcmp(name, attr->attr.name) == 0)
            break;
        attr = attr->next;
    }

    return attr;
}

const AstNode *findAttributeArgument(const AstNode *attr, cstring name)
{
    const AstNode *arg = attr->attr.args;
    while (arg) {
        if (strcmp(name, arg->fieldExpr.name) == 0)
            break;
        arg = arg->next;
    }
    return arg ? arg->fieldExpr.value : NULL;
}
