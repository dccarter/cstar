//
// Created by Carter on 2023-06-30.
//

#pragma once

#include <lang/ast.h>

struct msgpack_sbuffer;

bool convertAstNodeToBinary(struct msgpack_sbuffer *sbuf,
                            MemPool *pool,
                            const AstNode *node);
