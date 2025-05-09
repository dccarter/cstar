#include "core/strpool.h"
#include "core/hash.h"
#include "core/mempool.h"
#include "core/utils.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>

StrPool newStrPool(MemPool *mem_pool)
{
    return (StrPool){.mem_pool = mem_pool,
                     .hash_table = newHashTable(sizeof(char *))};
}

void freeStrPool(StrPool *str_pool) { freeHashTable(&str_pool->hash_table); }

typedef struct {
    const char *s;
    u64 len;
} SizedString;

static bool compareStrFind(const void *left, const void *right)
{
    SizedString *str = (SizedString *)right;
    return !strncmp(*(char **)left, str->s, str->len);
}

static bool compareStrInsert(const void *left, const void *right)
{
    SizedString *str = (SizedString *)right;
    return !strncmp(*(char **)left, str->s, str->len);
}

static cstring makeStringVargs(StrPool *strings, const char *fmt, va_list args)
{
    size_t size = 0;
    char *buf = NULL;
    FILE *fp = open_memstream(&buf, &size);
    csAssert0(fp);
    vfprintf(fp, fmt, args);
    fclose(fp);
    cstring str = makeStringSized(strings, buf, size);
    free(buf);
    return str;
}

const char *makeString(StrPool *str_pool, const char *str)
{
    return str ? makeStringSized(str_pool, str, strlen(str)) : NULL;
}

const char *makeTrimmedString(StrPool *pool, const char *str)
{
    if (str == NULL)
        return NULL;

    const char *s = str;
    while (*s && isspace(*s))
        s++;
    str = s;
    while (*s && !isspace(*s))
        s++;

    return makeStringSized(pool, str, s - str);
}

const char *makeStringSized(StrPool *pool, const char *str, u64 len)
{
    if (str == NULL)
        return NULL;
    uint32_t hash = hashRawBytes(hashInit(), str, len);
    SizedString s = {.s = str, .len = len};
    char **strPtr = findInHashTable(
        &pool->hash_table, &s, hash, sizeof(char *), compareStrFind);
    if (strPtr)
        return *strPtr;

    char *newStr = allocFromMemPool(pool->mem_pool, len + 1);
    memcpy(newStr, str, len);
    newStr[len] = 0;
    if (!insertInHashTable(
            &pool->hash_table, &newStr, hash, sizeof(char *), compareStrInsert))
        assert(false && "cannot insert string in string pool");
    return newStr;
}

const char *makeAnonymousVariable(StrPool *pool, const char *prefix)
{
    char variable[MAX_ANONYMOUS_PREFIX_SIZE + 32];
    static u64 postfix = 0;
    u64 len;
    if (prefix == NULL) {
        prefix = "cxy_anonymous_var";
        len = 19;
    }
    else
        len = strlen(prefix);

    csAssert0(len < +MAX_ANONYMOUS_PREFIX_SIZE);
    memcpy(variable, prefix, len);
    sprintf(&variable[len], "%" PRIu64, postfix++);

    return makeString(pool, variable);
}

const char *makeStringConcat_(StrPool *pool, const char *s1, ...)
{
    char variable[MAX_ANONYMOUS_PREFIX_SIZE + 32];
    size_t len = strlen(s1);
    csAssert0(len < MAX_ANONYMOUS_PREFIX_SIZE);
    memcpy(variable, s1, len);

    va_list ap;
    va_start(ap, s1);
    const char *s = va_arg(ap, const char *);
    while (s) {
        size_t sz = strlen(s);
        csAssert0(len < MAX_ANONYMOUS_PREFIX_SIZE);
        memcpy(&variable[len], s, sz);
        s = va_arg(ap, const char *);
        len += sz;
    }
    variable[len] = '\0';
    va_end(ap);

    return makeString(pool, variable);
}

cstring joinPath_(StrPool *pool, const char *p1, ...)
{
    char path[PATH_MAX];
    va_list ap;
    va_start(ap, p1);
    const char *s = p1;
    size_t len = 0;
    while (s) {
        size_t sz = strlen(s);
        const char *next = va_arg(ap, const char *);
        if (sz > 0) {
            csAssert0(sz + len < PATH_MAX);
            memcpy(&path[len], s, sz);
            len += sz;
            if (next && s[sz - 1] != '/')
                path[len++] = '/';
        }
        s = next;
    }
    path[len] = '\0';
    va_end(ap);
    return makeString(pool, path);
}

const char *makeStringf(StrPool *strings, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const char *str = makeStringVargs(strings, fmt, ap);
    va_end(ap);
    return str;
}
