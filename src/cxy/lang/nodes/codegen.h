/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-17
 */

#pragma once

#include "core/strpool.h"
#include "lang/ast.h"

typedef struct {
    FormatState *state;
    TypeTable *types;
    StrPool *strPool;
    cstring namespace;
} CodegenContext;

void writeNamespace(CodegenContext *ctx, cstring sep);
void writeEnumPrefix(CodegenContext *ctx, const Type *type);
void appendStringBuilderFunc(CodegenContext *ctx, const Type *type);
void generateTypeUsage(CodegenContext *ctx, const Type *type);
void writeTypename(CodegenContext *ctx, const Type *type);