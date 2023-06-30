//
// Created by Carter on 2023-06-29.
//

#pragma once

#include "3rdParty/cJSON.h"
#include "lang/visitor.h"

cJSON *convertToJson(MemPool *pool, const AstNode *node);
