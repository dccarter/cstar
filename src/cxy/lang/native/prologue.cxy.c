#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef float f32;
typedef double f64;

typedef const char *string;
typedef u32 wchar;
typedef u8 bool;

#define true 1
#define false 0
#define nullptr NULL

#define CXY_PASTE__(X, Y) X##Y
#define CXY_PASTE(X, Y) CXY_PASTE__(X, Y)

#define LINE_VAR(name) CXY_PASTE(name, __LINE__)

#define CXY_STR__(V) #V
#define CXY_STR(V) CXY_STR__(V)

#define sizeof__(A) (sizeof(A) / sizeof(*(A)))

#ifndef BIT
#define BIT(N) (1 << (N))
#endif

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

#if __has_attribute(always_inline)
#define cxy_always_inline() inline __attribute__((always_inline))
#else
#define cxy_always_inline()
#endif

#if __has_attribute(unused)
#define cxy_unused() __attribute__((unused))
#else
#define cxy_unused()
#endif

#if __has_attribute(noreturn)
#define cxy_noreturn() __attribute__((noreturn))
#else
#define cxy_noreturn()
#endif

#if __has_attribute(pure)
#define cxy_pure() __attribute__((pure))
#else
#define cxy_pure()
#endif

#if __has_attribute(warn_unused_result)
#define cxy_nodiscard() __attribute__((warn_unused_result))
#else
#define cxy_discard()
#endif

#if __has_attribute(packed)
#define cxy_packed() __attribute__((packed))
#else
#define cxy_packed()
#endif

#if __has_attribute(aligned)
#define cxy_aligned(S) __attribute__((packed, (S)))
#else
#warning                                                                       \
    "Align attribute not available, attempt to use cxy_aligned will cause an error"
#define cxy_aligned(state)                                                     \
    struct cxy_aligned_not_supported_on_current_platform {};
#endif

#if __has_attribute(cleanup)
#define cxy_cleanup(func) __attribute__((cleanup(func)))
#elif __has_attribute(__cleanup__)
#define cxy_cleanup(func) __attribute__((__cleanup__(func)))
#else
#warning                                                                       \
    "Cleanup attribute not available, attempt to use cxy_cleanup will cause an error"
#define cxy_cleanup(state)                                                     \
    struct cxy_clean_not_supported_on_current_platform {}
#endif

#if __has_attribute(format)
#define cxy_format(...) __attribute__((format(__VA_ARGS__)))
#else
#define cxy_format(...)
#endif

#if __has_attribute(fallthrough)
#define cxy_fallthrough() __attribute__((fallthrough))
#else
#define cxy_fallthrough() /* fall through */
#endif

#if __has_attribute(__builtin_unreachable)
#define unreachable(...)                                                       \
    do {                                                                       \
        csAssert(false, "Unreachable code reached");                           \
        __builtin_unreachable();                                               \
    } while (0)
#else
#define unreachable(...) csAssert(false, "Unreachable code reached");
#endif

#define attr(A, ...) CXY_PASTE(cxy_, A)(__VA_ARGS__)

#ifndef cxy_ALIGN
#define cxy_ALIGN(S, A) (((S) + ((A)-1)) & ~((A)-1))
#endif

#ifndef ptr
#define ptr(X) ((uintptr_t)(X))
#endif

enum {
    CXY_ALLOC_STATIC = 0b001,
    CXY_ALLOC_HEAP = 0b010,
    CXY_ALLOC_STACK = 0b100
};

#define CXY_MEMORY_MAGIC(ALLOC) 0xbebebe00 | CXY_ALLOC_##ALLOC

typedef struct cxy_memory_hdr_t {
    union {
        struct {
            u32 magic;
            u32 refs;
        };
        u64 hdr;
    };
} attr(packed) cxy_memory_hdr_t;

#define CXY_MEMORY_HEADER_SIZE sizeof(cxy_memory_hdr_t)
#define CXY_MEMORY_HEADER(PTR)                                                 \
    ((void *)(((u8 *)(PTR)) - CXY_MEMORY_HEADER_SIZE))
#define CXY_MEMORY_POINTER(HDR)                                                \
    ((void *)(((u8 *)(HDR)) + CXY_MEMORY_HEADER_SIZE))

static void *cxy_default_alloc(u64 size)
{
    cxy_memory_hdr_t *hdr = calloc(1, size + CXY_MEMORY_HEADER_SIZE);
    hdr->magic = CXY_MEMORY_MAGIC(HEAP);
    hdr->refs = 1;
    return CXY_MEMORY_POINTER(hdr);
}

static void *cxy_default_realloc(void *ptr, u64 size)
{
    cxy_memory_hdr_t *hdr =
        realloc(CXY_MEMORY_HEADER(ptr), size + CXY_MEMORY_HEADER_SIZE);
    hdr->magic = CXY_MEMORY_MAGIC(HEAP);
    hdr->refs = 1;
    return CXY_MEMORY_POINTER(hdr);
}

static void cxy_default_dealloc(void *ctx)
{
    cxy_memory_hdr_t *hdr = CXY_MEMORY_HEADER(ctx);
    if ((hdr->magic & CXY_MEMORY_MAGIC(HEAP)) && hdr->refs) {
        hdr->hdr = 0;
        free(hdr);
    }
}

#ifndef __builtin_alloc
#define __builtin_alloc(T, n) cxy_alloc(sizeof(T) * n)
#endif

#ifndef __builtin_realloc
#define __builtin_realloc(T, P, n) cxy_realloc((P), (sizeof(T) * n))
#endif

#ifndef __builtin_alloc_slice
#define __builtin_alloc_slice(T, n)                                            \
    (T) { .data = __builtin_alloc((*((T *)0)->data), (n)), .len = n }
#endif

#ifndef __builtin_realloc_slice
#define __builtin_realloc_slice(T, P, n)                                       \
    (T)                                                                        \
    {                                                                          \
        .data = __builtin_realloc((*((T *)0)->data), (P).data, (n)), .len = n  \
    }
#endif

static attr(noreturn)
    attr(format, printf, 1, 2) void cxyAbort(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    abort();
}

#define cxyAssert(COND, FMT, ...)                                              \
    if (!(COND))                                                               \
    cxyAbort("%s:%d : (" #COND ") " FMT "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#define csAssert(cond, ...) cxyAssert((cond), ##__VA_ARGS__)
#define csAssert0(cond) cxyAssert((cond), "")

#define cxy_stack_str_t(N)                                                     \
    _Static_assert(((N) <= 32), "Stack string's must be small");               \
    typedef struct Stack_str_##N##_t {                                         \
        char str[(N) + 1];                                                     \
    } cxy_stack_str_##N##_t

cxy_stack_str_t(4);
cxy_stack_str_t(8);
cxy_stack_str_t(16);
cxy_stack_str_t(32);

static cxy_stack_str_8_t cxy_wchar_str(wchar chr)
{
    if (chr < 0x80) {
        return (cxy_stack_str_8_t){
            .str = {[0] = (char)chr, [1] = '\0', [5] = 1}};
    }
    else if (chr < 0x800) {
        return (cxy_stack_str_8_t){.str = {[0] = (char)(0xC0 | (chr >> 6)),
                                           [1] = (char)(0x80 | (chr & 0x3F)),
                                           [3] = '\0',
                                           [5] = 2}};
    }
    else if (chr < 0x10000) {
        return (cxy_stack_str_8_t){
            .str = {[0] = (char)(0xE0 | (chr >> 12)),
                    [1] = (char)(0x80 | ((chr >> 6) & 0x3F)),
                    [2] = (char)(0x80 | (chr & 0x3F)),
                    [3] = '\0',
                    [5] = 3}};
    }
    else if (chr < 0x200000) {
        return (cxy_stack_str_8_t){
            .str = {[0] = (char)(0xF0 | (chr >> 18)),
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

static inline u64 fwputc(wchar c, FILE *io)
{
    cxy_stack_str_8_t s = cxy_wchar_str(c);
    return fwrite(s.str, 1, s.str[5], io);
}

static inline u64 wputc(wchar c)
{
    cxy_stack_str_8_t s = cxy_wchar_str(c);
    s.str[4] = '\n';
    s.str[5] += 1;
    return fwrite(s.str, 1, s.str[5], stdout);
}

typedef struct {
    u64 size;
    char *data;
} cxy_string_t;

cxy_string_t *cxy_string_new0(const char *cstr, u64 len)
{
    cxy_string_t *str = cxy_alloc(sizeof(cxy_string_t) + len + 1);
    str->size = len;
    if (cstr != NULL)
        memcpy(str->data, cstr, len);
    str->data[len] = '\0';
    return str;
}

attr(always_inline) cxy_string_t *cxy_string_new1(const char *cstr)
{
    return cxy_string_new0(cstr, strlen(cstr));
}

attr(always_inline) cxy_string_t *cxy_string_dup(const cxy_string_t *str)
{
    return cxy_string_new0(str->data, str->size);
}

cxy_string_t *cxy_string_concat(const cxy_string_t *s1, const cxy_string_t *s2)
{
    cxy_string_t *str = cxy_string_new0(NULL, s1->size + s2->size);
    memcpy(str->data, s1->data, s1->size);
    memcpy(&str->data[s1->size], s2->data, s2->size);
    return str;
}

attr(always_inline) void cxy_string_delete(cxy_string_t *str) { free(str); }

#ifndef cxy_STRING_BUILDER_DEFAULT_CAPACITY
#define cxy_STRING_BUILDER_DEFAULT_CAPACITY 32
#endif

typedef struct {
    u64 capacity;
    u64 size;
    char *data;
} cxy_string_builder_t;

void cxy_string_builder_grow(cxy_string_builder_t *sb, u64 size)
{
    if (sb->data == NULL) {
        sb->data = malloc(size + 1);
        sb->capacity = size;
    }
    else if (size > (sb->capacity - sb->size)) {
        while (sb->capacity < sb->size + size) {
            sb->capacity <<= 1;
        }
        sb->data = realloc(sb->data, sb->capacity + 1);
    }
}

attr(always_inline) void cxy_string_builder_init(cxy_string_builder_t *sb)
{
    cxy_string_builder_grow(sb, cxy_STRING_BUILDER_DEFAULT_CAPACITY);
}

cxy_string_builder_t *cxy_string_builder_new()
{
    cxy_string_builder_t *sb = calloc(1, sizeof(cxy_string_builder_t));
    cxy_string_builder_init(sb);
    return sb;
}

void cxy_string_builder_deinit(cxy_string_builder_t *sb)
{
    if (sb->data)
        free(sb->data);
    memset(sb, 0, sizeof(*sb));
}

attr(always_inline) void cxy_string_builder_delete(cxy_string_builder_t *sb)
{
    if (sb)
        free(sb);
}

void cxy_string_builder_append_cstr0(cxy_string_builder_t *sb,
                                     const char *cstr,
                                     u64 len)
{
    cxy_string_builder_grow(sb, len);
    memmove(&sb->data[sb->size], cstr, len);
    sb->size += len;
    sb->data[sb->size] = '\0';
}

attr(always_inline) void cxy_string_builder_append_cstr1(
    cxy_string_builder_t *sb, const char *cstr)
{
    cxy_string_builder_append_cstr0(sb, cstr, strlen(cstr));
}

attr(always_inline) void cxy_string_builder_append_int(cxy_string_builder_t *sb,
                                                       i64 num)
{
    char data[32];
    i64 len = sprintf(data, "%lld", num);
    cxy_string_builder_append_cstr0(sb, data, len);
}

attr(always_inline) void cxy_string_builder_append_float(
    cxy_string_builder_t *sb, f64 num)
{
    char data[32];
    i64 len = sprintf(data, "%g", num);
    cxy_string_builder_append_cstr0(sb, data, len);
}

attr(always_inline) void cxy_string_builder_append_char(
    cxy_string_builder_t *sb, wchar c)
{
    cxy_stack_str_8_t s = cxy_wchar_str(c);
    cxy_string_builder_append_cstr0(sb, s.str, s.str[5]);
}

attr(always_inline) void cxy_string_builder_append_bool(
    cxy_string_builder_t *sb, bool v)
{
    if (v)
        cxy_string_builder_append_cstr0(sb, "true", 4);
    else
        cxy_string_builder_append_cstr0(sb, "false", 5);
}

char *cxy_string_builder_release(cxy_string_builder_t *sb)
{
    char *data = sb->data;
    sb->data = NULL;
    cxy_string_builder_deinit(sb);
    return data;
}

typedef struct {
    u64 value;
    const char *name;
} cxy_enum_names_t;

const char *cxy_enum_find_name(const cxy_enum_names_t *names, u64 value)
{
    const cxy_enum_names_t *name = names;
    for (; name->name != NULL; name++) {
        if (name->value == value)
            return name->name;
    }

    return "(Unknown)";
}
