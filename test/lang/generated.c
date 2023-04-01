/**
 * Generated from cxy compile
 */

/* --------------------- epilogue.cxy.c --------------*/

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef int8_t f32;
typedef int8_t f64;

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
        return (__cxy_stack_str_8_t){.str = {[0] = chr, [1] = '\n'}};
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

/* --------------------- Generated PROLOGUE --------------*/

typedef struct {
  i64 _0;
  i64 _1;
  i64 _2;
} __cxy_anonymous_tuple_18_t;
typedef string __cxy_anonymous_array_25_t[];
typedef i32(*__cxy_anonymous_func_26_t)();

/* --------------------- Generated EPILOGUE --------------*/



__cxy_anonymous_tuple_18_t range(i64 start, i64 end, i64 step) {
  return (__cxy_anonymous_tuple_18_t){._0 = start, ._1 = end, ._2 = step};
}

const __cxy_anonymous_tuple_18_t* next(const __cxy_anonymous_tuple_18_t* range) {
  const i32 x = 20;
  return range;
}

extern i32 puts(string str);

#define __CXY_MAIN_INVOKE(...) return __cxy_main(__VA_ARGS__)

i32 __cxy_main(__cxy_anonymous_array_25_t args) {
  const __cxy_anonymous_tuple_18_t x = range(10, 20, 10);
  const __cxy_anonymous_func_26_t f = /* <unsupported AST tag 42> */;
  wputc(128515);
  wputc(48);
  return 0;
}
/* --------------------- epilogue.cxy.c --------------*/

typedef const char **__cxy_main_argv_t;

int main(int argc, const char *argv[])
{
    __cxy_main_argv_t args = argv;
    __CXY_MAIN_INVOKE(args);
}

