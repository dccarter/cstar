
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STYLE_NORMAL = 0,
    STYLE_BOLD,
    STYLE_DIM,
    STYLE_UNDERLINE,
    STYLE_ITALIC
} Style;

typedef enum {
    COLOR_NORMAL = 0,
    COLOR_RED,
    COLOR_GREEN,
    COLOR_BLUE,
    COLOR_CYAN,
    COLOR_MAGENTA,
    COLOR_YELLOW,
    COLOR_WHITE
} Color;

typedef struct FormatBuf {
    size_t size;
    size_t capacity;
    struct FormatBuf *next;
    char data[];
} FormatBuf;

typedef struct {
    Style style;
    Color color;
} FormatStyle;

typedef struct Type Type;

typedef union {
    FormatStyle style;
    const Type *t;
    bool b;
    const void *p;
    const char *s;
    uint32_t c;
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
    uintmax_t u;
    int8_t i8;
    int16_t i16;
    int32_t i32;
    int64_t i64;
    intmax_t i;
    float f32;
    double f64;
    size_t len;
} FormatArg;

typedef struct {
    FormatBuf *currentBuf;
    FormatBuf *firstBuf;
    bool ignoreStyle;
    size_t indent;
    const char *tab;
} FormatState;

static const FormatStyle resetStyle = {STYLE_NORMAL, COLOR_NORMAL};
static const FormatStyle errorStyle = {STYLE_BOLD, COLOR_RED};
static const FormatStyle literalStyle = {STYLE_NORMAL, COLOR_MAGENTA};
static const FormatStyle keywordStyle = {STYLE_BOLD, COLOR_BLUE};
static const FormatStyle commentStyle = {STYLE_NORMAL, COLOR_GREEN};
static const FormatStyle ellipsisStyle = {STYLE_NORMAL, COLOR_WHITE};
static const FormatStyle locStyle = {STYLE_BOLD, COLOR_WHITE};

FormatState newFormatState(const char *tab, bool ignoreStyle);
void freeFormatState(FormatState *);

void format(FormatState *, const char *format_str, const FormatArg *args);
void append(FormatState *, const char *s, size_t bytes);
void printWithStyle(FormatState *, const char *, FormatStyle);
void printKeyword(FormatState *, const char *);
void printUtf8(FormatState *state, uint32_t);
void writeFormatState(FormatState *, FILE *);
char *formatStateToString(FormatState *);

#ifdef __cplusplus
}
#endif
