//
// Created by Carter on 2023-06-30.
//

#pragma once

#include "ast.h"

#ifdef __cplusplus
extern "C" {
#endif

struct msgpack_sbuffer;

bool binaryEncodeAstNode(struct msgpack_sbuffer *sbuf,
                         MemPool *pool,
                         const AstNode *node);

AstNode *binaryDecodeAstNode(const void *encoded, size_t size);

#ifdef __cplusplus
}
#endif
