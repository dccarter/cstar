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
#define cyn_always_inline() inline __attribute__((always_inline))
#else
#define cyn_always_inline()
#endif

#if __has_attribute(unused)
#define cyn_unused() __attribute__((unused))
#else
#define cyn_unused()
#endif

#if __has_attribute(noreturn)
#define cyn_noreturn() __attribute__((noreturn))
#else
#define cyn_noreturn()
#endif

#if __has_attribute(pure)
#define cyn_pure() __attribute__((pure))
#else
#define cyn_pure()
#endif

#if __has_attribute(warn_unused_result)
#define cyn_nodiscard() __attribute__((warn_unused_result))
#else
#define cyn_discard()
#endif

#if __has_attribute(packed)
#define cyn_packed() __attribute__((packed))
#else
#define cyn_packed()
#endif

#if __has_attribute(aligned)
#define cyn_aligned(S) __attribute__((packed, (S)))
#else
#warning                                                                       \
    "Align attribute not available, attempt to use cyn_aligned will cause an error"
#define cyn_aligned(state)                                                     \
    struct cyn_aligned_not_supported_on_current_platform {};
#endif

#if __has_attribute(cleanup)
#define cyn_cleanup(func) __attribute__((cleanup(func)))
#elif __has_attribute(__cleanup__)
#define cyn_cleanup(func) __attribute__((__cleanup__(func)))
#else
#warning                                                                       \
    "Cleanup attribute not available, attempt to use cyn_cleanup will cause an error"
#define cyn_cleanup(state)                                                     \
    struct cyn_clean_not_supported_on_current_platform {}
#endif

#if __has_attribute(format)
#define cyn_format(...) __attribute__((format(__VA_ARGS__)))
#else
#define cyn_format(...)
#endif

#if __has_attribute(fallthrough)
#define cyn_fallthrough() __attribute__((fallthrough))
#else
#define cyn_fallthrough() /* fall through */
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

#define attr(A, ...) CXY_PASTE(cyn_, A)(__VA_ARGS__)

#ifndef CYN_ALIGN
#define CYN_ALIGN(S, A) (((S) + ((A)-1)) & ~((A)-1))
#endif

static attr(noreturn)
    attr(format, printf, 1, 2) void cynAbort(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    abort();
}

#define cynAssert(COND, FMT, ...)                                              \
    if (!(COND))                                                               \
    cynAbort("%s:%d : (" #COND ") " FMT "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#define csAssert(cond, ...) cynAssert((cond), ##__VA_ARGS__)
#define csAssert0(cond) cynAssert((cond), "")

#define __cxy_stack_str_t(N)                                                   \
    _Static_assert(((N) <= 32), "Stack string's must be small");               \
    typedef struct Stack_str_##N##_t {                                         \
        char str[(N) + 1];                                                     \
    } __cxy_stack_str_##N##_t

__cxy_stack_str_t(4);
__cxy_stack_str_t(8);
__cxy_stack_str_t(16);
__cxy_stack_str_t(32);

static __cxy_stack_str_8_t __cxy_wchar_str(wchar chr)
{
    int i = 0;
    if (chr < 0x80) {
        return (__cxy_stack_str_8_t){.str = {[0] = chr, [1] = '\0', [5] = 1}};
    }
    else if (chr < 0x800) {
        return (__cxy_stack_str_8_t){.str = {[0] = (char)(0xC0 | (chr >> 6)),
                                             [1] = (char)(0x80 | (chr & 0x3F)),
                                             [3] = '\0',
                                             [5] = 2}};
    }
    else if (chr < 0x10000) {
        return (__cxy_stack_str_8_t){
            .str = {[0] = (char)(0xE0 | (chr >> 12)),
                    [1] = (char)(0x80 | ((chr >> 6) & 0x3F)),
                    [2] = (char)(0x80 | (chr & 0x3F)),
                    [3] = '\0',
                    [5] = 3}};
    }
    else if (chr < 0x200000) {
        return (__cxy_stack_str_8_t){
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
    __cxy_stack_str_8_t s = __cxy_wchar_str(c);
    return fwrite(s.str, 1, s.str[5], io);
}

typedef struct __cxy_range_t {
    i64 start, end, step, curr;
} __cxy_range_t;

inline void __cxy_range_init(
    __cxy_range_t *this, i64 start, i64 end, i64 step, i64 curr)
{
    this->start = start;
    this->end = end;
}

static inline int wputc(wchar c)
{
    __cxy_stack_str_8_t s = __cxy_wchar_str(c);
    s.str[4] = '\n';
    s.str[5] += 1;
    return fwrite(s.str, 1, s.str[5], stdout);
}

typedef struct {
    u64 size;
    char data[0];
} __cxy_string_t;

__cxy_string_t *__cxy_string_new0(const char *cstr, u64 len)
{
    __cxy_string_t *str = calloc(sizeof(__cxy_string_t) + len + 1, 1);
    str->size = len;
    if (cstr != NULL)
        memcpy(str->data, cstr, len);
    str->data[len] = '\0';
    return str;
}

attr(always_inline) __cxy_string_t *__cxy_string_new1(const char *cstr)
{
    return __cxy_string_new0(cstr, strlen(cstr));
}

attr(always_inline) __cxy_string_t *__cxy_string_dup(const __cxy_string_t *str)
{
    return __cxy_string_new0(str->data, str->size);
}

__cxy_string_t *__cxy_string_concat(const __cxy_string_t *s1,
                                    const __cxy_string_t *s2)
{
    __cxy_string_t *str = __cxy_string_new0(NULL, s1->size + s2->size);
    memcpy(str->data, s1->data, s1->size);
    memcpy(&str->data[s1->size], s2->data, s2->size);
    return str;
}

attr(always_inline) void __cxy_string_delete(__cxy_string_t *str) { free(str); }

#ifndef __CXY_STRING_BUILDER_DEFAULT_CAPACITY
#define __CXY_STRING_BUILDER_DEFAULT_CAPACITY 32
#endif

typedef struct {
    u64 capacity;
    u64 size;
    char *data;
} __cxy_string_builder_t;

void __cxy_string_builder_grow(__cxy_string_builder_t *sb, u64 size)
{
    size;
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

attr(always_inline) void __cxy_string_builder_init(__cxy_string_builder_t *sb)
{
    __cxy_string_builder_grow(sb, __CXY_STRING_BUILDER_DEFAULT_CAPACITY);
}

__cxy_string_builder_t *__cxy_string_builder_new()
{
    __cxy_string_builder_t *sb = calloc(1, sizeof(__cxy_string_builder_t));
    __cxy_string_builder_init(sb);
    return sb;
}

void __cxy_string_builder_deinit(__cxy_string_builder_t *sb)
{
    if (sb->data)
        free(sb->data);
    memset(sb, 0, sizeof(*sb));
}

attr(always_inline) void __cxy_string_builder_delete(__cxy_string_builder_t *sb)
{
    if (sb)
        free(sb);
}

void __cxy_string_builder_append_cstr0(__cxy_string_builder_t *sb,
                                       const char *cstr,
                                       u64 len)
{
    __cxy_string_builder_grow(sb, len);
    memmove(&sb->data[sb->size], cstr, len);
    sb->size += len;
    sb->data[sb->size] = '\0';
}

attr(always_inline) void __cxy_string_builder_append_cstr1(
    __cxy_string_builder_t *sb, const char *cstr)
{
    __cxy_string_builder_append_cstr0(sb, cstr, strlen(cstr));
}

attr(always_inline) void __cxy_string_builder_append_int(
    __cxy_string_builder_t *sb, i64 num)
{
    char data[32];
    i64 len = sprintf(data, "%lld", num);
    __cxy_string_builder_append_cstr0(sb, data, len);
}

attr(always_inline) void __cxy_string_builder_append_float(
    __cxy_string_builder_t *sb, f64 num)
{
    char data[32];
    i64 len = sprintf(data, "%g", num);
    __cxy_string_builder_append_cstr0(sb, data, len);
}

attr(always_inline) void __cxy_string_builder_append_char(
    __cxy_string_builder_t *sb, wchar c)
{
    __cxy_stack_str_8_t s = __cxy_wchar_str(c);
    __cxy_string_builder_append_cstr0(sb, s.str, s.str[5]);
}

attr(always_inline) void __cxy_string_builder_append_bool(
    __cxy_string_builder_t *sb, bool v)
{
    if (v)
        __cxy_string_builder_append_cstr0(sb, "true", 4);
    else
        __cxy_string_builder_append_cstr0(sb, "false", 5);
}

char *__cxy_string_builder_release(__cxy_string_builder_t *sb)
{
    char *data = sb->data;
    sb->data = NULL;
    __cxy_string_builder_deinit(sb);
    return data;
}
