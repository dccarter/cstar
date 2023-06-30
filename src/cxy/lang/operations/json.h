//
// Created by Carter on 2023-06-29.
//

#pragma once

#include "3rdParty/cJSON.h"
#include "lang/visitor.h"

typedef struct {
    bool includeLocation;
    bool withoutAttrs;
    bool withNamedEnums;
} AstNodeToJsonConfig;

cJSON *convertToJson(AstNodeToJsonConfig *config,
                     MemPool *pool,
                     const AstNode *node);
