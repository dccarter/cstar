#include "core/utils.h"
#include "core/alloc.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>

#ifdef WIN32
#define isatty _isatty
#define fileno _fileno
#include <io.h>
#else
#include <string.h>
#include <unistd.h>
#endif

#ifndef NDEBUG
#define CHUNK_SIZE 4
#else
#define CHUNK_SIZE 4096
#endif

typedef Pair(u32, u32) u32_u32_pair;

static size_t convertStrToCharOrd(const char *ptr, int base, u32 *res)
{
    char *next = NULL;
    unsigned int ord = strtoul(ptr, &next, base);
    *res = ord;
    return ord <= 255 && !errno ? next - ptr : 0;
}

static u32 inline countLeadingZeros(char c)
{
    for (int i = 7; i >= 0; i--) {
        if ((c & (1 << i)) == 0)
            return 7 - i;
    }
    return 8;
}

static u32_u32_pair convertStrToUtf32(const char *s, size_t count)
{
    u32 len = countLeadingZeros(s[0]);

    if (len == 0) {
        return (u32_u32_pair){1, (u32)s[0]};
    }
    csAssert(count <= len, "invalid UTF-8 character sequence");

    switch (len) {
    case 2:
        return (u32_u32_pair){2, ((s[0] & 0x1F) << 6) | (s[1] & 0x3F)};
    case 3:
        return (u32_u32_pair){
            3, ((s[0] & 0xF) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F)};
    case 4:
        return (u32_u32_pair){4,
                              ((s[0] & 0x7) << 18) | ((s[1] & 0x3F) << 12) |
                                  ((s[2] & 0x3F) << 6) | (s[3] & 0x3F)};
    default:
        csAssert(false, "invalid UTF-8 sequence");
    }

    unreachable("");
}

size_t convertEscapeSeq(const char *ptr, size_t n, u32 *res)
{
    if (n == 0)
        return 0;
    if (ptr[0] == '\\') {
        if (n <= 1)
            return 0;
        switch (ptr[1]) {
        case '0':
            *res = '\0';
            return 2;
        case 'n':
            *res = '\n';
            return 2;
        case 't':
            *res = '\t';
            return 2;
        case 'v':
            *res = '\v';
            return 2;
        case 'r':
            *res = '\r';
            return 2;
        case 'a':
            *res = '\a';
            return 2;
        case 'b':
            *res = '\b';
            return 2;
        case '$':
            *res = '$';
            return 2;
        case 'x':
            if (n <= 2)
                return 0;
            return convertStrToCharOrd(ptr + 2, 16, res);
        default:
            if (isdigit(ptr[1]))
                return convertStrToCharOrd(ptr + 1, 8, res);
            return 0;
        }
    }
    if (((u8 *)ptr)[0] >= 0x80) {
        u32_u32_pair p = convertStrToUtf32(ptr, n);
        *res = p.s;
        return p.f;
    }

    *res = (u8)ptr[0];
    return 1;
}

size_t escapeString(const char *str, size_t n, char *dst, size_t size)
{
    csAssert0(size >= n);

    u64 i = 0, j = 0;

    while (i < n) {
        if (str[i] != '\\') {
            dst[j++] = str[i++];
            continue;
        }

        i++;
        if ((n - i) < 1)
            continue;

        switch (str[i]) {
        case '0':
            dst[j++] = '\0';
            i++;
            break;
        case 'n':
            dst[j++] = '\n';
            i++;
            break;
        case 't':
            dst[j++] = '\t';
            i++;
            break;
        case 'v':
            dst[j++] = '\v';
            i++;
            break;
        case 'r':
            dst[j++] = '\r';
            i++;
            break;
        case 'a':
            dst[j++] = '\a';
            i++;
            break;
        case 'b':
            dst[j++] = '\b';
            i++;
            break;
        case '$':
            dst[j++] = '$';
            i++;
            break;
        default:
            dst[j++] = '\\';
            break;
        }
    }

    dst[j] = 0;
    return j;
}

bool isColorSupported(FILE *file) { return isatty(fileno(file)); }

char *readFile(const char *fileName, size_t *file_size)
{
    FILE *file = fopen(fileName, "rb");
    if (!file)
        return NULL;
    size_t chunk_size = CHUNK_SIZE;
    char *file_data = NULL;
    *file_size = 0;
    while (true) {
        if (ferror(file)) {
            fclose(file);
            free(file_data);
            return NULL;
        }
        file_data = reallocOrDie(file_data, *file_size + chunk_size);
        size_t read_count = fread(file_data + *file_size, 1, chunk_size, file);
        *file_size += read_count;
        if (read_count < chunk_size)
            break;
        chunk_size *= 2;
    }
    fclose(file);

    // Add terminator
    file_data = reallocOrDie(file_data, *file_size + 1);
    file_data[*file_size] = 0;
    return file_data;
}

int binarySearch(const void *arr,
                 u64 len,
                 const void *x,
                 u64 size,
                 int (*compare)(const void *, const void *))
{
    int lower = 0;
    int upper = (int)len - 1;
    const u8 *ptr = arr;
    while (lower <= upper) {
        int mid = lower + (upper - lower) / 2;
        int res = compare(x, ptr + (size * mid));
        if (res == 0)
            return mid;

        if (res > 0)
            lower = mid + 1;
        else
            upper = mid - 1;
    }
    return -1;
}

int compareStrings(const void *lhs, const void *rhs)
{
    return strcmp((cstring)lhs, (cstring)rhs);
}

void cxyAbort(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    abort();
}
