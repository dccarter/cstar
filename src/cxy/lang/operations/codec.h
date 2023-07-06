//
// Created by Carter on 2023-06-30.
//

#pragma once

#include <lang/ast.h>

struct msgpack_sbuffer;

bool binaryEncodeAstNode(struct msgpack_sbuffer *sbuf,
                         MemPool *pool,
                         const AstNode *node);

AstNode *binaryDecodeAstNode(const void *encoded, size_t size);
