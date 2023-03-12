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
