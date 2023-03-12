#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CYN_PASTE__(X, Y) X##Y
#define CynPST(X, Y) CYN_PASTE__(X, Y)

#define LineVAR(name)      CynPST(name, __LINE__)

#define CYN_STR__(V) #V
#define CynSTR(V) CYN_STR__(V)

#ifndef CYN_VERSION_MAJOR
#define CYN_VERSION_MAJOR 0
#endif

#ifndef CYN_VERSION_MINOR
#define CYN_VERSION_MINOR 1
#endif

#ifndef CYN_VERSION_PATCH
#define CYN_VERSION_PATCH 0
#endif

#define CYN_VERSION         \
    CynSTR(CYN_VERSION_MAJOR) "." CynSTR(CYN_VERSION_MINOR) "." CynSTR(CYN_VERSION_PATCH)

#ifdef __BASE_FILE__
#define CYN_FILENAME __BASE_FILE__
#else
#define CYN_FILENAME ((strrchr(__FILE__, '/')?: __FILE__ - 1) + 1)
#endif

#define sizeof__(A) (sizeof(A)/sizeof(*(A)))

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
#warning "Align attribute not available, attempt to use cyn_aligned will cause an error"
#define cyn_aligned(S) struct cyn_aligned_not_supported_on_current_platform{};
#endif

#if __has_attribute(cleanup)
#define cyn_cleanup(func) __attribute__((cleanup(func)))
#elif __has_attribute(__cleanup__)
#define cyn_cleanup(func) __attribute__((__cleanup__(func)))
#else
#warning "Cleanup attribute not available, attempt to use cyn_cleanup will cause an error"
#define cyn_cleanup(S) struct cyn_clean_not_supported_on_current_platform{}
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
#define unreachable(...) do {                   \
    assert(!"Unreachable code reached");        \
    __builtin_unreachable();                    \
} while(0)
#else
#define unreachable(...) assert(!"Unreachable code reached");
#endif

#define attr(A, ...) CynPST(cyn_, A)(__VA_ARGS__)

#ifndef CynAlign
#define CynAlign(S, A) (((S) + ((A)-1)) & ~((A)-1))
#endif

typedef uint8_t  u8;
typedef int8_t   i8;
typedef uint16_t u16;
typedef int16_t  i16;
typedef uint32_t u32;
typedef int32_t  i32;
typedef uint64_t u64;
typedef int64_t  i64;
typedef float    f32;
typedef double   f64;
typedef uintptr_t uptr;
typedef const char* cstring;

#define MIN(A, B) ({ __typeof__(A) _A = (A); __typeof__(B) _B = (B); _A < _B? _A : _B; })
#define MAX(A, B) ({ __typeof__(A) _A = (A); __typeof__(B) _B = (B); _A > _B? _A : _B; })

#define unalignedLoad(T, k) ({ T LineVAR(k); memcpy(&LineVAR(k), k, sizeof(LineVAR(k))); LineVAR(k); })

#define Stack_str_(N)  \
    _Static_assert(((N) <= 32), "Stack string's must be small"); \
    typedef struct Stack_str_##N##_t { char str[(N)+1]; } Stack_str_##N

Stack_str_(4);
Stack_str_(8);
Stack_str_(16);
Stack_str_(32);

#define Stack_str(N) Stack_str_##N
#define Format_to_ss(SS, fmt, ...) snprintf((SS).str, sizeof((SS).str)-1, fmt, __VA_ARGS__)

attr(noreturn)
attr(format, printf, 1, 2)
void cynAbort(const char *fmt, ...);

#define cynAssert(COND, FMT, ...) \
    if (!(COND)) cynAbort("%s:%d : (" #COND ") " FMT "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#define csAssert(cond, ...)  cynAssert((cond), ##__VA_ARGS__)
#define csAssert0(cond) cynAssert((cond), "")

#define cDEF    "\x1B[0m"
#define cRED    "\x1B[32m"
#define cGRN    "\x1B[32m"
#define cYLW    "\x1B[33m"
#define cBLU    "\x1B[34m"
#define cMGN    "\x1B[35m"
#define cCYN    "\x1B[36m"
#define cWHT    "\x1B[37m"

#define cBOLD   "\x1B[1;0m"
#define cBRED   "\x1B[1;32m"
#define cBGRN   "\x1B[1;32m"
#define cBYLW   "\x1B[1;33m"
#define cBBLU   "\x1B[1;34m"
#define cBMGN   "\x1B[1;35m"
#define cBCYN   "\x1B[1;36m"
#define cBWHT   "\x1B[1;37m"

#define Pair(T1, T2) struct { T1 f; T2 s; }
#define unpack(A, B, P)                     \
    __typeof(P) LineVAR(uPp) = (P);         \
    __typeof__((P).f) A = LineVAR(uPp).f;   \
    __typeof__((P).s) B = LineVAR(uPp).s

#define make(T, ...) ((T){ __VA_ARGS__ })
#define New(A, T, ...)                                      \
    ({                                                      \
        T* LineVAR(aNa) = Allocator_alloc(A, sizeof(T));    \
        *LineVAR(aNa) = make(T, __VA_ARGS__);               \
        LineVAR(aNa);                                       \
    })

static inline unsigned ilog2(uintmax_t i)
{
    unsigned p = 0;
    while (i > 0)
        p++, i >>= 1;
    return p;
}

size_t convertEscapeSeq(const char *str, size_t n, char *res);
bool isColorSupported(FILE *);
char *readFile(const char *fileName, size_t *file_size);
