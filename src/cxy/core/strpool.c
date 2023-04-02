#include "core/strpool.h"
#include "core/hash.h"
#include "core/mempool.h"
#include "core/utils.h"

#include <assert.h>
#include <string.h>

StrPool newStrPool(MemPool *mem_pool)
{
    return (StrPool){.mem_pool = mem_pool,
                     .hash_table = newHashTable(sizeof(char *))};
}

void freeStrPool(StrPool *str_pool) { freeHashTable(&str_pool->hash_table); }

static bool compareStrs(const void *left, const void *right)
{
    return !strcmp(*(char **)left, *(char **)right);
}

const char *makeString(StrPool *str_pool, const char *str)
{
    if (!str)
        return NULL;
    uint32_t hash = hashStr(hashInit(), str);
    char **str_ptr = findInHashTable(
        &str_pool->hash_table, &str, hash, sizeof(char *), compareStrs);
    if (str_ptr)
        return *str_ptr;
    size_t len = strlen(str);
    char *new_str = allocFromMemPool(str_pool->mem_pool, len + 1);
    memcpy(new_str, str, len);
    new_str[len] = 0;
    if (!insertInHashTable(
            &str_pool->hash_table, &new_str, hash, sizeof(char *), compareStrs))
        assert(false && "cannot insert string in string pool");
    return new_str;
}

const char *makeAnonymousVariable(StrPool *pool, const char *prefix)
{
    static char variable[MAX_ANONYMOUS_PREFIX_SIZE + 32];
    static u64 postfix = 0;
    u64 len;
    if (prefix == NULL) {
        prefix = "__cxy_anonymous_var";
        len = 19;
    }
    else
        len = strlen(prefix);

    if (len > MAX_ANONYMOUS_PREFIX_SIZE)
        return NULL;
    memcpy(variable, prefix, len);
    sprintf(&variable[len], "_%llu", postfix++);

    return makeString(pool, variable);
}
