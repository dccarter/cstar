//
// Created by Carter on 2023-03-28.
//

#pragma once

#include <core/strpool.h>
#include <lang/ast.h>

typedef struct TypeTable TypeTable;

void semanticsCheck(AstNode *program,
                    Log *L,
                    MemPool *pool,
                    StrPool *strPool,
                    TypeTable *typeTable);
