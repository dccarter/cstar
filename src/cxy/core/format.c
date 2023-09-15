#include "core/format.h"
#include "core/alloc.h"
#include "lang/types.h"

#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FORMAT_CHARS 64
#define DEFAULT_BUF_CAPACITY 1024

static FormatBuf *allocBuf(size_t capacity)
{
    FormatBuf *buf = mallocOrDie(sizeof(FormatBuf) + capacity);
    buf->size = 0;
    buf->capacity = capacity;
    buf->next = NULL;
    return buf;
}

static char *reserveBuf(FormatState *state, size_t size)
{
    if (state->currentBuf &&
        state->currentBuf->size + size <= state->currentBuf->capacity)
        return state->currentBuf->data + state->currentBuf->size;
    size_t capacity =
        state->currentBuf ? state->currentBuf->size : DEFAULT_BUF_CAPACITY;
    if (capacity < size)
        capacity = size;
    FormatBuf *buf = allocBuf(capacity);
    if (state->currentBuf)
        state->currentBuf->next = buf;
    else
        state->firstBuf = buf;
    state->currentBuf = buf;
    return buf->data;
}

static void advanceBuf(FormatBuf *buf, size_t inc)
{
    assert(buf->capacity >= buf->size + inc);
    buf->size += inc;
}

static void write(FormatState *state, const char *ptr, size_t size)
{
    memcpy(reserveBuf(state, size), ptr, size);
    advanceBuf(state->currentBuf, size);
}

static void writeChar(FormatState *state, char c) { write(state, &c, 1); }

static void writeStr(FormatState *state, const char *s)
{
    write(state, s, strlen(s));
}

static void printUtf8ManyTimes(FormatState *state,
                               uint32_t chr,
                               bool escaped,
                               u64 times)
{
    for (u64 i = 0; i < times; i++)
        printUtf8(state, chr, escaped);
}

static const char *formatArg(FormatState *state,
                             const char *ptr,
                             size_t *index,
                             const FormatArg *args)
{
    const FormatArg *arg = &args[(*index)++];
    char *bufPtr = reserveBuf(state, MAX_FORMAT_CHARS);
    size_t n = 0;
    char c = *(ptr++);
    switch (c) {
    case 'u':
        switch (*ptr) {
        case '8':
            ptr += 1;
            n = snprintf(bufPtr, MAX_FORMAT_CHARS, "%" PRIu8, arg->u8);
            break;
        case '1':
            assert(ptr[1] == '6');
            ptr += 2;
            n = snprintf(bufPtr, MAX_FORMAT_CHARS, "%" PRIu16, arg->u16);
            break;
        case '3':
            assert(ptr[1] == '2');
            ptr += 2;
            n = snprintf(bufPtr, MAX_FORMAT_CHARS, "%" PRIu32, arg->u32);
            break;
        case '6':
            assert(ptr[1] == '4');
            ptr += 2;
            n = snprintf(bufPtr, MAX_FORMAT_CHARS, "%" PRIu64, arg->u64);
            break;
        default:
            n = snprintf(bufPtr, MAX_FORMAT_CHARS, "%" PRIuMAX, arg->u);
            break;
        }
        break;
    case 'i':
        switch (*ptr) {
        case '8':
            ptr += 1;
            n = snprintf(bufPtr, MAX_FORMAT_CHARS, "%" PRIi8, arg->i8);
            break;
        case '1':
            assert(ptr[1] == '6');
            ptr += 2;
            n = snprintf(bufPtr, MAX_FORMAT_CHARS, "%" PRIi16, arg->i16);
            break;
        case '3':
            assert(ptr[1] == '2');
            ptr += 2;
            n = snprintf(bufPtr, MAX_FORMAT_CHARS, "%" PRIi32, arg->i32);
            break;
        case '6':
            assert(ptr[1] == '4');
            ptr += 2;
            n = snprintf(bufPtr, MAX_FORMAT_CHARS, "%" PRIi64, arg->i64);
            break;
        default:
            n = snprintf(bufPtr, MAX_FORMAT_CHARS, "%" PRIiMAX, arg->i);
            break;
        }
        break;
    case 'f':
        switch (*(ptr++)) {
        case '3':
            assert(*ptr == '2');
            ptr++;
            n = snprintf(bufPtr, MAX_FORMAT_CHARS, "%e", arg->f32);
            break;
        case '6':
            assert(*ptr == '4');
            ptr++;
            n = snprintf(bufPtr, MAX_FORMAT_CHARS, "%e", arg->f64);
            break;
        default:
            assert(false && "invalid floating-point format string");
        }
        break;
    case 'c':
        if (*ptr == 'E') {
            ptr++;
            if (*ptr == 'l') {
                ptr++;
                printUtf8ManyTimes(state, arg->c, true, args[(*index)++].len);
            }
            else
                printUtf8(state, arg->c, true);
        }
        else {
            if (*ptr == 'l') {
                ptr++;
                printUtf8ManyTimes(state, arg->c, false, args[(*index)++].len);
            }
            else
                printUtf8(state, arg->c, false);
        }
        break;
    case 's':
        if (*ptr == 'l') {
            ptr++;
            write(state, arg->s, args[(*index)++].len);
        }
        else
            writeStr(state, arg->s);
        break;
    case 'b':
        writeStr(state, arg->b ? "true" : "false");
        break;
    case 'p':
        n = snprintf(bufPtr, MAX_FORMAT_CHARS, "%p", arg->p);
        break;
    case 't':
        printType(state, arg->t);
        break;
    default:
        assert(false && "unknown formatting command");
        break;
    }
    assert(n < MAX_FORMAT_CHARS);
    advanceBuf(state->currentBuf, n);
    return ptr;
}

static void applyStyle(FormatState *state, const FormatArg *arg)
{
    if (arg->style.style == STYLE_NORMAL || arg->style.color == COLOR_NORMAL) {
        writeStr(state, "\33[0m");
        if (arg->style.style == STYLE_NORMAL &&
            arg->style.color == COLOR_NORMAL)
            return;
    }

    writeStr(state, "\33[");
    switch (arg->style.style) {
    case STYLE_BOLD:
        writeChar(state, '1');
        break;
    case STYLE_DIM:
        writeChar(state, '2');
        break;
    case STYLE_UNDERLINE:
        writeChar(state, '4');
        break;
    case STYLE_ITALIC:
        writeChar(state, '3');
        break;
    default:
        break;
    }
    if (arg->style.style != STYLE_NORMAL)
        writeChar(state, ';');
    switch (arg->style.color) {
    case COLOR_RED:
        writeStr(state, "31");
        break;
    case COLOR_GREEN:
        writeStr(state, "32");
        break;
    case COLOR_BLUE:
        writeStr(state, "34");
        break;
    case COLOR_CYAN:
        writeStr(state, "36");
        break;
    case COLOR_MAGENTA:
        writeStr(state, "35");
        break;
    case COLOR_YELLOW:
        writeStr(state, "33");
        break;
    case COLOR_WHITE:
        writeStr(state, "37");
        break;
    default:
        break;
    }
    writeChar(state, 'm');
}

FormatState newFormatState(const char *tab, bool ignoreStyle)
{
    return (FormatState){.tab = tab, .ignoreStyle = ignoreStyle};
}

void freeFormatState(FormatState *state)
{
    FormatBuf *next = NULL;
    for (FormatBuf *buf = state->firstBuf; buf; buf = next) {
        next = buf->next;
        free(buf);
    }
}

void format(FormatState *state, const char *format_str, const FormatArg *args)
{
    const char *ptr = format_str;
    size_t index = 0;
    while (*ptr) {
        const char *prev = ptr;
        ptr += strcspn(ptr, "\n{");
        write(state, prev, ptr - prev);
        if (*ptr == '\n') {
            ptr++;
            writeChar(state, '\n');
            for (size_t i = 0, n = state->indent; i < n; ++i)
                writeStr(state, state->tab);
        }
        else if (*ptr == '{') {
            switch (*(++ptr)) {
            case '{':
                writeChar(state, '{');
                ptr++;
                continue;
            case '>':
                state->indent++;
                ptr++;
                break;
            case '<':
                assert(state->indent > 0);
                state->indent--;
                ptr++;
                break;
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                index = strtoull(ptr, (char **)&ptr, 10);
                assert(*ptr == ':' && "positional separator expected");
                ptr++;
                // fallthrough
            default:
                if (*ptr == '$') {
                    if (!state->ignoreStyle)
                        applyStyle(state, &args[index]);
                    index++, ptr++;
                }
                else
                    ptr = formatArg(state, ptr, &index, args);
                break;
            }
            assert(*ptr == '}' && "argument end marker expected");
            ptr++;
        }
    }
}

void printWithStyle(FormatState *state, const char *str, FormatStyle style)
{
    format(state,
           "{$}{s}{$}",
           (FormatArg[]){{.style = style}, {.s = str}, {.style = resetStyle}});
}

void printKeyword(FormatState *state, const char *keyword)
{
    printWithStyle(state, keyword, keywordStyle);
}

void printUtf8(FormatState *state, uint32_t chr, bool escaped)
{
    if (escaped) {
        switch (chr) {
        case '\0':
            writeStr(state, "\\0");
            return;
        case '\n':
            writeStr(state, "\\n");
            return;
        case '\t':
            writeStr(state, "\\t");
            return;
        case '\v':
            writeStr(state, "\\v");
            return;
        case '\r':
            writeStr(state, "\\r");
            return;
        case '\a':
            writeStr(state, "\\a");
            return;
        case '\b':
            writeStr(state, "\\b");
            return;
        default:
            break;
        }
    }

    if (chr < 0x80) {
        writeChar(state, (char)chr);
    }
    else if (chr < 0x800) {
        char c[] = {
            (char)(0xC0 | (chr >> 6)), (char)(0x80 | (chr & 0x3F)), '\0'};
        writeStr(state, c);
    }
    else if (chr < 0x10000) {
        char c[] = {(char)(0xE0 | (chr >> 12)),
                    (char)(0x80 | ((chr >> 6) & 0x3F)),
                    (char)(0x80 | (chr & 0x3F)),
                    '\0'};
        writeStr(state, c);
    }
    else if (chr < 0x200000) {
        char c[] = {(char)(0xF0 | (chr >> 18)),
                    (char)(0x80 | ((chr >> 12) & 0x3F)),
                    (char)(0x80 | ((chr >> 6) & 0x3F)),
                    (char)(0x80 | (chr & 0x3F)),
                    '\0'};
        writeStr(state, c);
    }
    else {
        unreachable("!!!invalid UCS character: \\U%08x", chr);
    }
}

void writeFormatState(const FormatState *state, FILE *file)
{
    for (FormatBuf *buf = state->firstBuf; buf; buf = buf->next)
        fwrite(buf->data, 1, buf->size, file);
}

char *formatStateToString(FormatState *state)
{
    u64 size = 0;
    for (FormatBuf *buf = state->firstBuf; buf; buf = buf->next)
        size += buf->size;

    char *str = mallocOrDie(size + 1);
    u64 copied = 0;
    for (FormatBuf *buf = state->firstBuf; buf; buf = buf->next) {
        memcpy(&str[copied], buf->data, buf->size);
        copied += buf->size;
    }
    str[copied] = '\0';
    return str;
}

void append(FormatState *state, const char *s, size_t bytes)
{
    write(state, s, bytes);
}
