/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-27
 */

#pragma once

#include <core/utils.h>

#include <string.h>

#ifndef __cxy_builtins_string_builder_DEFAULT_CAPACITY
#define __cxy_builtins_string_builder_DEFAULT_CAPACITY 32
#endif

typedef struct {
    u64 capacity;
    u64 size;
    char *data;
} StringBuilder;

Stack_str_8 wcharToStr(wchar chr);

void stringBuilderGrow(StringBuilder *sb, u64 size);

static attr(always_inline) void stringBuilderInit(StringBuilder *sb)
{
    stringBuilderGrow(sb, __cxy_builtins_string_builder_DEFAULT_CAPACITY);
}

StringBuilder *stringBuilderNew();

void stringBuilderDeinit(StringBuilder *sb);

static attr(always_inline) void stringBuilderDelete(StringBuilder *sb)
{
    if (sb) {
        stringBuilderInit(sb);
        free(sb);
    }
}

void stringBuilderAppendCstr0(StringBuilder *sb, const char *cstr, u64 len);

static attr(always_inline) void stringBuilderAppendCstr1(StringBuilder *sb,
                                                         const char *cstr)
{
    stringBuilderAppendCstr0(sb, cstr, strlen(cstr));
}

static attr(always_inline) void stringBuilderAppendChar(StringBuilder *sb,
                                                        wchar c)
{
    struct Stack_str_8_t s = wcharToStr(c);
    stringBuilderAppendCstr0(sb, s.str, s.str[5]);
}

void stringBuilderAppendInt(StringBuilder *sb, i64 num);
void stringBuilderAppendFloat(StringBuilder *sb, f64 num);
void stringBuilderAppendBool(StringBuilder *sb, bool v);
char *stringBuilderRelease(StringBuilder *sb);
