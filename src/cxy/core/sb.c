/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-27
 */

#include "sb.h"

#include "alloc.h"

#include <inttypes.h>

Stack_str_8 wcharToStr(wchar chr)
{
    if (chr < 0x80) {
        return (Stack_str_8){.str = {[0] = (char)chr, [1] = '\0', [5] = 1}};
    }
    else if (chr < 0x800) {
        return (Stack_str_8){.str = {[0] = (char)(0xC0 | (chr >> 6)),
                                     [1] = (char)(0x80 | (chr & 0x3F)),
                                     [3] = '\0',
                                     [5] = 2}};
    }
    else if (chr < 0x10000) {
        return (Stack_str_8){.str = {[0] = (char)(0xE0 | (chr >> 12)),
                                     [1] = (char)(0x80 | ((chr >> 6) & 0x3F)),
                                     [2] = (char)(0x80 | (chr & 0x3F)),
                                     [3] = '\0',
                                     [5] = 3}};
    }
    else if (chr < 0x200000) {
        return (Stack_str_8){.str = {[0] = (char)(0xF0 | (chr >> 18)),
                                     [1] = (char)(0x80 | ((chr >> 12) & 0x3F)),
                                     [2] = (char)(0x80 | ((chr >> 6) & 0x3F)),
                                     [3] = (char)(0x80 | (chr & 0x3F)),
                                     [4] = '\0',
                                     [5] = 4}};
    }
    else {
        unreachable("!!!Invalid UCS character: \\U%08x", chr);
    }
}

void stringBuilderGrow(StringBuilder *sb, u64 size)
{
    if (sb->data == NULL) {
        sb->data = mallocOrDie(size + 1);
        sb->capacity = size;
    }
    else if (size > (sb->capacity - sb->size)) {
        while (sb->capacity < sb->size + size) {
            sb->capacity <<= 1;
        }
        sb->data = reallocOrDie(sb->data, sb->capacity + 1);
    }
}

StringBuilder *stringBuilderNew()
{
    StringBuilder *sb = calloc(1, sizeof(StringBuilder));
    stringBuilderInit(sb);
    return sb;
}

void stringBuilderDeinit(StringBuilder *sb)
{
    if (sb->data)
        free(sb->data);
    memset(sb, 0, sizeof(*sb));
}

void stringBuilderAppendCstr0(StringBuilder *sb, const char *cstr, u64 len)
{
    stringBuilderGrow(sb, len);
    memmove(&sb->data[sb->size], cstr, len);
    sb->size += len;
    sb->data[sb->size] = '\0';
}

void stringBuilderAppendInt(StringBuilder *sb, i64 num)
{
    char data[32];
    i64 len = sprintf(data, "%" PRId64, num);
    stringBuilderAppendCstr0(sb, data, len);
}

void stringBuilderAppendFloat(StringBuilder *sb, f64 num)
{
    char data[32];
    i64 len = sprintf(data, "%g", num);
    stringBuilderAppendCstr0(sb, data, len);
}

void stringBuilderAppendBool(StringBuilder *sb, bool v)
{
    if (v)
        stringBuilderAppendCstr0(sb, "true", 4);
    else
        stringBuilderAppendCstr0(sb, "false", 5);
}

char *stringBuilderRelease(StringBuilder *sb)
{
    char *data = sb->data;
    sb->data = NULL;
    stringBuilderDeinit(sb);
    return data;
}
