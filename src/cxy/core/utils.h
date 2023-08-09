#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CXY_PASTE__(X, Y) X##Y
#define CXY_PASTE(X, Y) CXY_PASTE__(X, Y)
#define CXY_PASTE_XYZ__(X, Y, Z) X##Y##Z
#define CXY_PASTE_XYZ(X, Y, Z) CXY_PASTE_XYZ__(X, Y, Z)

#define LINE_VAR(name) CXY_PASTE(name, __LINE__)

#define CXY_STR__(V) #V
#define CXY_STR(V) CXY_STR__(V)

#ifndef CXY_VERSION_MAJOR
#define CXY_VERSION_MAJOR 0
#endif

#ifndef CXY_VERSION_MINOR
#define CXY_VERSION_MINOR 1
#endif

#ifndef CXY_VERSION_PATCH
#define CXY_VERSION_PATCH 0
#endif

#define CXY_VERSION_STR                                                        \
    CXY_STR(CXY_VERSION_MAJOR)                                                 \
    "." CXY_STR(CXY_VERSION_MINOR) "." CXY_STR(CXY_VERSION_PATCH)

#ifdef __BASE_FILE__
#define CXY_FILENAME __BASE_FILE__
#else
#define CXY_FILENAME ((strrchr(__FILE__, '/') ?: __FILE__ - 1) + 1)
#endif

#define sizeof__(A) (sizeof(A) / sizeof(*(A)))

#ifndef BIT
#define BIT(N) (((u64)1) << (N))
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
#define cxy_cleanup(enclosure) __attribute__((__cleanup__(enclosure)))
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
        assert(!"Unreachable code reached");                                   \
        __builtin_unreachable();                                               \
    } while (0)
#else
#define unreachable(...) csAssert(false, "Unreachable code reached");
#endif

#define attr(A, ...) CXY_PASTE(cxy_, A)(__VA_ARGS__)

#ifndef cxy_ALIGN
#define cxy_ALIGN(S, A) (((S) + ((A)-1)) & ~((A)-1))
#endif

typedef uint8_t u8;
typedef int8_t i8;
typedef uint16_t u16;
typedef int16_t i16;
typedef uint32_t u32;
typedef int32_t i32;
typedef uint64_t u64;
typedef int64_t i64;
typedef float f32;
typedef double f64;
typedef uintptr_t uptr;
typedef const char *cstring;
typedef u32 wchar;

#define MIN(A, B)                                                              \
    ({                                                                         \
        __typeof__(A) _A = (A);                                                \
        __typeof__(B) _B = (B);                                                \
        _A < _B ? _A : _B;                                                     \
    })
#define MAX(A, B)                                                              \
    ({                                                                         \
        __typeof__(A) _A = (A);                                                \
        __typeof__(B) _B = (B);                                                \
        _A > _B ? _A : _B;                                                     \
    })

#define unalignedLoad(T, k)                                                    \
    ({                                                                         \
        T LINE_VAR(k);                                                         \
        memcpy(&LINE_VAR(k), k, sizeof(LINE_VAR(k)));                          \
        LINE_VAR(k);                                                           \
    })

#define __stack_str_t(N)                                                       \
    _Static_assert(((N) <= 32), "Stack string's must be small");               \
    typedef struct Stack_str_##N##_t {                                         \
        char str[(N) + 1];                                                     \
    } Stack_str_##N

__stack_str_t(4);
__stack_str_t(8);
__stack_str_t(16);
__stack_str_t(32);

#define Stack_str(N) Stack_str_##N
#define Format_to_ss(SS, fmt, ...)                                             \
    snprintf((SS).str, sizeof((SS).str) - 1, fmt, __VA_ARGS__)

attr(noreturn) attr(format, printf, 1, 2) void cxyAbort(const char *fmt, ...);

#define cxyAssert(COND, FMT, ...)                                              \
    if (!(COND))                                                               \
    cxyAbort("%s:%d : (" #COND ") " FMT "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#define csAssert(cond, ...) cxyAssert((cond), ##__VA_ARGS__)
#define csAssert0(cond) cxyAssert((cond), "")

#define cDEF "\x1B[0m"
#define cRED "\x1B[32m"
#define cGRN "\x1B[32m"
#define cYLW "\x1B[33m"
#define cBLU "\x1B[34m"
#define cMGN "\x1B[35m"
#define ccxy "\x1B[36m"
#define cWHT "\x1B[37m"

#define cBOLD "\x1B[1;0m"
#define cBRED "\x1B[1;32m"
#define cBGRN "\x1B[1;32m"
#define cBYLW "\x1B[1;33m"
#define cBBLU "\x1B[1;34m"
#define cBMGN "\x1B[1;35m"
#define cBcxy "\x1B[1;36m"
#define cBWHT "\x1B[1;37m"

#define Pair(T1, T2)                                                           \
    struct {                                                                   \
        T1 f;                                                                  \
        T2 s;                                                                  \
    }
#define unpack(A, B, P)                                                        \
    __typeof(P) LINE_VAR(uPp) = (P);                                           \
    __typeof__((P).f) A = LINE_VAR(uPp).f;                                     \
    __typeof__((P).s) B = LINE_VAR(uPp).s

#define make(T, ...) ((T){__VA_ARGS__})
#define New(P, T, ...)                                                         \
    ({                                                                         \
        T *LINE_VAR(aNa) = allocFromMemPool((P), sizeof(T));                   \
        *LINE_VAR(aNa) = make(T, __VA_ARGS__);                                 \
        LINE_VAR(aNa);                                                         \
    })

static inline unsigned ilog2(uintmax_t i)
{
    unsigned p = 0;
    while (i > 0)
        p++, i >>= 1;
    return p;
}

size_t convertEscapeSeq(const char *str, size_t n, u32 *res);
bool isColorSupported(FILE *);
char *readFile(const char *fileName, size_t *file_size);
int binarySearch(const void *arr,
                 u64 len,
                 const void *x,
                 u64 size,
                 int (*compare)(const void *, const void *));

static inline int binarySearchWithRef(const void *arr,
                                      u64 len,
                                      const void *x,
                                      u64 size,
                                      int (*compare)(const void *,
                                                     const void *))
{
    return binarySearch(arr, len, &x, size, compare);
}

int compareStrings(const void *lhs, const void *rhs);

static inline bool isIgnoreVar(cstring s)
{
    return s && s[0] == '_' && s[1] == '\0';
}

static inline u64 timespecToNanoSeconds(struct timespec *ts)
{
    return ts->tv_sec * 1000000000 + ts->tv_nsec;
}

static inline u64 timespecToMicroSeconds(struct timespec *ts)
{
    return ts->tv_sec * 1000000 + ts->tv_nsec / 1000;
}

static inline u64 timespecToMilliseconds(struct timespec *ts)
{
    return ts->tv_sec * 1000 + ts->tv_nsec / 1000000;
}

#ifdef __cplusplus
}
#endif
