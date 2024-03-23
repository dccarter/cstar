#include "core/hash.h"

/*
 * Note: This is an implementation of the FNV-1a hashing function.
 * See
 * https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
 */

HashCode hashInit() { return UINT32_C(0x811c9dc5); }

HashCode hashPtr(HashCode h, const void *ptr)
{
    return hashUint64(h, (ptrdiff_t)ptr);
}

HashCode hashUint8(HashCode h, uint8_t x) { return (h ^ x) * 0x01000193; }

HashCode hashUint16(HashCode h, uint16_t x)
{
    return hashUint8(hashUint8(h, x), x >> 8);
}

HashCode hashUint32(HashCode h, HashCode x)
{
    return hashUint16(hashUint16(h, x), x >> 16);
}

HashCode hashUint64(HashCode h, uint64_t x)
{
    return hashUint32(hashUint32(h, x), x >> 32);
}

HashCode hashStr(HashCode h, const char *str)
{
    while (*str)
        h = hashUint8(h, *(str++));
    return h;
}

HashCode hashRawBytes(HashCode h, const void *ptr, size_t size)
{
    for (size_t i = 0; i < size; ++i)
        h = hashUint8(h, ((char *)ptr)[i]);
    return h;
}
