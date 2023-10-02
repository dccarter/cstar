#ifndef CXY_SETUP_CODE
#define CXY_SETUP_CODE

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
        CXY__assert(false, "Unreachable code reached");                        \
        __builtin_unreachable();                                               \
    } while (0)
#else
#define unreachable(...) CXY__assert(false, "Unreachable code reached");
#endif

#define attr(A, ...) CXY_PASTE(cxy_, A)(__VA_ARGS__)

#ifndef CXY_ALIGN
#define CXY_ALIGN(S, A) (((S) + ((A)-1)) & ~((A)-1))
#endif

#ifndef ptr
#define ptr(X) ((uintptr_t)(X))
#endif

enum {
    CXY_ALLOC_STATIC = 0b001,
    CXY_ALLOC_HEAP = 0b010,
    CXY_ALLOC_STACK = 0b100
};

#define CXY_MEMORY_MAGIC(ALLOC) (0xbebebe00 | CXY_ALLOC_##ALLOC)

typedef struct CXY__memory_hdr_t {
    void (*destructor)(void *);

    union {
        struct {
            u32 refs;
            u32 magic;
        };
        u64 hdr;
    };
} attr(packed) CXY__memory_hdr_t;

#define CXY_MEMORY_HEADER_SIZE sizeof(CXY__memory_hdr_t)
#define CXY_MEMORY_HEADER(PTR)                                                 \
    ((void *)(((u8 *)(PTR)) - CXY_MEMORY_HEADER_SIZE))
#define CXY_MEMORY_POINTER(HDR)                                                \
    ((void *)(((u8 *)(HDR)) + CXY_MEMORY_HEADER_SIZE))

static void *CXY__default_alloc(u64 size, void (*destructor)(void *))
{
    CXY__memory_hdr_t *hdr = malloc(size + CXY_MEMORY_HEADER_SIZE);
    hdr->magic = CXY_MEMORY_MAGIC(HEAP);
    hdr->refs = 1;
    hdr->destructor = destructor;
    return CXY_MEMORY_POINTER(hdr);
}

static void *CXY__default_calloc(u64 n, u64 size, void (*destructor)(void *))
{
    CXY__memory_hdr_t *hdr = calloc(n, size + CXY_MEMORY_HEADER_SIZE);
    hdr->magic = CXY_MEMORY_MAGIC(HEAP);
    hdr->refs = 1;
    hdr->destructor = destructor;
    return CXY_MEMORY_POINTER(hdr);
}

void *CXY__default_realloc(void *ptr, u64 size, void (*destructor)(void *));
void CXY__default_dealloc(void *ptr);

attr(always_inline) void *CXY__default_get_ref(void *ptr)
{
    if (ptr) {
        CXY__memory_hdr_t *hdr = CXY_MEMORY_HEADER(ptr);
        if (hdr->magic == CXY_MEMORY_MAGIC(HEAP))
            hdr->refs++;
    }
    return ptr;
}

#ifndef CXY__alloc
#define CXY__alloc CXY__default_alloc
#define CXY__free CXY__default_dealloc
#define CXY__realloc CXY__default_realloc
#define CXY__calloc CXY__default_calloc
#endif

#ifndef CXY__builtins_alloc
#define CXY__builtins_alloc(T, n, destructor)                                  \
    CXY__alloc((sizeof(T) * (n)), (destructor))
#endif

#ifndef CXY__builtins_dealloc
#define CXY__builtins_dealloc(P) CXY__free((void *)(P))
#endif

#ifndef CXY__builtins_realloc
#define CXY__builtins_realloc(T, P, n, destructor)                             \
    CXY__realloc((P), (sizeof(T) * (n)), (destructor))
#endif

#ifndef CXY__builtins_get_ref
#define CXY__builtins_get_ref(P) CXY__default_get_ref((void *)(P))
#endif

typedef struct CXY__slice_t {
    u64 len;
    void *data;
    u8 p[0];
} CXY__slice_t;

void *CXY__alloc_slice_(u64 count, u64 size, void (*destructor)(void *));

void *CXY__realloc_slice_(void *ptr,
                          u64 count,
                          u64 size,
                          void (*destructor)(void *));

#ifndef CXY__builtins_alloc_slice
#define CXY__builtins_alloc_slice(T, n, destructor)                            \
    (T) CXY__alloc_slice_((n), sizeof(((T)0)->data[0]), (destructor))
#endif

#ifndef CXY__builtins_realloc_slice
#define CXY__builtins_realloc_slice(T, P, n, destructor)                       \
    (T) CXY__realloc_slice_((P), (n), sizeof(((T)0)->data[0]), (destructor))
#endif

#ifndef CXY__builtins_memset_slice
#define CXY__builtins_memset_slice(T, P, C)                                    \
    memset((P)->data, (C), (sizeof(((T)0)->data[0]) * (P)->len))
#endif

#ifndef CXY__builtins_assert
#define CXY__builtins_assert(cond, file, line, pos)                            \
    if (!(cond))                                                               \
    cxyAbort("assertion failed (" #cond ") : %s:%d:%d\n", file, line, pos)
#endif

#ifndef CXY__builtins_sizeof
#define CXY__builtins_sizeof(X) sizeof(X)
#endif

attr(always_inline) void CXY__stack_free(void *ptr)
{
    union {
        void **stack;
        void *ptr;
    } either = {.ptr = ptr};
    CXY__free(*either.stack);
    *either.stack = NULL;
}

#define CXY__stack_cleanup __attribute__((cleanup(CXY__stack_free)))

attr(noreturn) attr(format, printf, 1, 2) void cxyAbort(const char *fmt, ...);

#define CXY__assert(COND, FMT, ...)                                            \
    if (!(COND))                                                               \
    cxyAbort("%s:%d : (" #COND ") " FMT "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#define CXY__stack_str_t(N)                                                    \
    _Static_assert(((N) <= 32), "Stack string's must be small");               \
    typedef struct Stack_str_##N##_t {                                         \
        char str[(N) + 1];                                                     \
    } CXY__stack_str_##N##_t

CXY__stack_str_t(4);
CXY__stack_str_t(8);
CXY__stack_str_t(16);
CXY__stack_str_t(32);

static CXY__stack_str_8_t cxy_wchar_str(wchar chr)
{
    if (chr < 0x80) {
        return (CXY__stack_str_8_t){
            .str = {[0] = (char)chr, [1] = '\0', [5] = 1}};
    }
    else if (chr < 0x800) {
        return (CXY__stack_str_8_t){.str = {[0] = (char)(0xC0 | (chr >> 6)),
                                            [1] = (char)(0x80 | (chr & 0x3F)),
                                            [3] = '\0',
                                            [5] = 2}};
    }
    else if (chr < 0x10000) {
        return (CXY__stack_str_8_t){
            .str = {[0] = (char)(0xE0 | (chr >> 12)),
                    [1] = (char)(0x80 | ((chr >> 6) & 0x3F)),
                    [2] = (char)(0x80 | (chr & 0x3F)),
                    [3] = '\0',
                    [5] = 3}};
    }
    else if (chr < 0x200000) {
        return (CXY__stack_str_8_t){
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

attr(always_inline) static u64 fwputc(wchar c, FILE *io)
{
    CXY__stack_str_8_t s = cxy_wchar_str(c);
    return fwrite(s.str, 1, s.str[5], io);
}

attr(always_inline) static u64 wputc(wchar c)
{
    CXY__stack_str_8_t s = cxy_wchar_str(c);
    s.str[4] = '\n';
    s.str[5] += 1;
    return fwrite(s.str, 1, s.str[5], stdout);
}

typedef struct {
    u64 size;
    char *data;
} CXY__string_t;

attr(always_inline) static void CXY__string_delete(void *str)
{
    CXY__string_t *this = str;
    CXY__free(this->data);
    this->data = nullptr;
    this->data = 0;
}

static CXY__string_t *CXY__string_new0(const char *cstr, u64 len)
{
    CXY__string_t *str =
        CXY__alloc(sizeof(CXY__string_t) + len + 1, CXY__string_delete);
    str->size = len;
    if (cstr != NULL)
        memcpy(str->data, cstr, len);
    str->data[len] = '\0';
    return str;
}

attr(always_inline) static CXY__string_t *CXY__string_new1(const char *cstr)
{
    return CXY__string_new0(cstr, strlen(cstr));
}

static attr(always_inline)
    CXY__string_t *CXY__string_dup(const CXY__string_t *str)
{
    return CXY__string_new0(str->data, str->size);
}

static CXY__string_t *CXY__string_concat(const CXY__string_t *s1,
                                         const CXY__string_t *s2)
{
    CXY__string_t *str = CXY__string_new0(NULL, s1->size + s2->size);
    memcpy(str->data, s1->data, s1->size);
    memcpy(&str->data[s1->size], s2->data, s2->size);
    return str;
}

#ifndef __CXY_STRING_BUILDER_DEFAULT_CAPACITY
#define __CXY_STRING_BUILDER_DEFAULT_CAPACITY 32
#endif

typedef struct {
    u64 capacity;
    u64 size;
    char *data;
} CXY__builtins_string_builder_t;

static void CXY__builtins_string_builder_delete_fwd(void *sb);

void CXY__builtins_string_builder_grow(CXY__builtins_string_builder_t *sb,
                                       u64 size);

attr(always_inline) void CXY__builtins_string_builder_init(
    CXY__builtins_string_builder_t *sb)
{
    CXY__builtins_string_builder_grow(sb,
                                      __CXY_STRING_BUILDER_DEFAULT_CAPACITY);
}

CXY__builtins_string_builder_t *CXY__builtins_string_builder_new();

attr(always_inline) void CXY__builtins_string_builder_deinit(
    CXY__builtins_string_builder_t *sb)
{
    if (sb->data)
        free(sb->data);
    memset(sb, 0, sizeof(*sb));
}

attr(always_inline) void CXY__builtins_string_builder_delete(
    CXY__builtins_string_builder_t *sb)
{
    if (sb->data) {
        CXY__free(sb->data);
        *sb = (CXY__builtins_string_builder_t){0, 0, NULL};
    }
}

static void CXY__builtins_string_builder_delete_fwd(void *sb)
{
    CXY__builtins_string_builder_delete((CXY__builtins_string_builder_t *)sb);
}

void CXY__builtins_string_builder_append_cstr0(
    CXY__builtins_string_builder_t *sb, const char *cstr, u64 len);

attr(always_inline) static void CXY__builtins_string_builder_append_cstr1(
    CXY__builtins_string_builder_t *sb, const char *cstr)
{
    CXY__builtins_string_builder_append_cstr0(
        sb, cstr, cstr ? strlen(cstr) : 0);
}

attr(always_inline) static void CXY__builtins_string_builder_append_int(
    CXY__builtins_string_builder_t *sb, i64 num)
{
    char data[32];
    i64 len = sprintf(data, "%lld", num);
    CXY__builtins_string_builder_append_cstr0(sb, data, len);
}

attr(always_inline) static void CXY__builtins_string_builder_append_float(
    CXY__builtins_string_builder_t *sb, f64 num)
{
    char data[32];
    i64 len = sprintf(data, "%g", num);
    CXY__builtins_string_builder_append_cstr0(sb, data, len);
}

attr(always_inline) static void CXY__builtins_string_builder_append_ptr(
    CXY__builtins_string_builder_t *sb, const void *ptr)
{
    char data[32];
    i64 len = sprintf(data, "%p", ptr);
    CXY__builtins_string_builder_append_cstr0(sb, data, len);
}

attr(always_inline) static void CXY__builtins_string_builder_append_char(
    CXY__builtins_string_builder_t *sb, wchar c)
{
    CXY__stack_str_8_t s = cxy_wchar_str(c);
    CXY__builtins_string_builder_append_cstr0(sb, s.str, s.str[5]);
}

attr(always_inline) static void CXY__builtins_string_builder_append_bool(
    CXY__builtins_string_builder_t *sb, bool v)
{
    if (v)
        CXY__builtins_string_builder_append_cstr0(sb, "true", 4);
    else
        CXY__builtins_string_builder_append_cstr0(sb, "false", 5);
}

attr(always_inline) static char *CXY__builtins_string_builder_release(
    CXY__builtins_string_builder_t *sb)
{
    char *data = sb->data;
    sb->data = NULL;
    CXY__builtins_string_builder_deinit(sb);
    return data;
}

attr(always_inline) static u64
    CXY__builtins_string_builder_size(CXY__builtins_string_builder_t *sb)
{
    return sb->size;
}

int CXY__builtins_binary_search(const void *arr,
                                u64 len,
                                const void *x,
                                u64 size,
                                int (*compare)(const void *, const void *));

typedef struct {
    i64 value;
    const char *name;
} CXY__enum_name_t;

const char *CXY__enum_find_name(const CXY__enum_name_t *names,
                                u64 count,
                                u64 value);

typedef uint32_t CXY__hash_code_t;

attr(always_inline) CXY__hash_code_t CXY__hash_fnv1a_init()
{
#define FNV_32_INIT UINT32_C(0x811c9dc5)
    return FNV_32_INIT;
#undef FNV_32_INIT
}

attr(always_inline) CXY__hash_code_t
    CXY__hash_fnv1a_uint8(CXY__hash_code_t h, uint8_t x)
{
#define FNV_32_PRIME 0x01000193
    return (h ^ x) * FNV_32_PRIME;
#undef FNV_32_PRIME
}

attr(always_inline) CXY__hash_code_t
    CXY__hash_fnv1a_uint16(CXY__hash_code_t h, uint16_t x)
{
    return CXY__hash_fnv1a_uint8(CXY__hash_fnv1a_uint8(h, x), x >> 8);
}

attr(always_inline) CXY__hash_code_t
    CXY__hash_fnv1a_uint32(CXY__hash_code_t h, CXY__hash_code_t x)
{
    return CXY__hash_fnv1a_uint16(CXY__hash_fnv1a_uint16(h, x), x >> 16);
}

attr(always_inline) CXY__hash_code_t
    CXY__hash_fnv1a_uint64(CXY__hash_code_t h, uint64_t x)
{
    return CXY__hash_fnv1a_uint32(CXY__hash_fnv1a_uint32(h, x), x >> 32);
}

attr(always_inline) CXY__hash_code_t
    CXY__hash_fnv1a_ptr(CXY__hash_code_t h, const void *ptr)
{
    return CXY__hash_fnv1a_uint64(h, (ptrdiff_t)ptr);
}

attr(always_inline) CXY__hash_code_t
    CXY__hash_fnv1a_string(CXY__hash_code_t h, const char *str)
{
    while (*str)
        h = CXY__hash_fnv1a_uint8(h, *(str++));
    return h;
}

attr(always_inline) CXY__hash_code_t
    CXY__hash_fnv1a_bytes(CXY__hash_code_t h, const void *ptr, u64 size)
{
    for (u64 i = 0; i < size; ++i)
        h = CXY__hash_fnv1a_uint8(h, ((char *)ptr)[i]);
    return h;
}

#define CXY_MIN_PRIME 7
#define CXY_MAX_PRIME 1048583
#define CXY_PRIMES(f)                                                          \
    f(CXY_MIN_PRIME) f(17) f(31) f(67) f(257) f(1031) f(4093) f(8191) f(16381) \
        f(32381) f(65539) f(131071) f(262147) f(524287) f(CXY_MAX_PRIME)

static const u64 cxy_primes[] = {
#define f(x) x,
    CXY_PRIMES(f)
#undef f
};

// Returns the prime that is strictly greater than the given value.
// If there is no such prime in the list, returns MAX_PRIME.
static u64 CXY__hash_next_prime(u64 i)
{
    u64 j = 0, k = sizeof__(cxy_primes);
    while (j < k) {
        u64 m = (j + k) / 2;
        u64 p = cxy_primes[m];
        if (p <= i)
            j = m + 1;
        else
            k = m;
    }
    return cxy_primes[k >= sizeof__(cxy_primes) ? sizeof__(cxy_primes) - 1 : k];
}

// Returns the modulus of a number i by a prime p.
static u64 CXY__hash_mod_prime(u64 i, u64 p)
{
    switch (p) {
#define f(x)                                                                   \
    case x:                                                                    \
        return i % x;
        CXY_PRIMES(f)
#undef f
    default:
        return i % p;
    }
}

#endif
