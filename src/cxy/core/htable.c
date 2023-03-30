#include "core/htable.h"
#include "core/alloc.h"
#include "core/primes.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define OCCUPIED_MASK UINT32_C(0x80000000)
#define DEFAULT_HASH_TABLE_CAPACITY 4
#define MAX_LOAD_FACTOR 70 //%

static inline size_t incrementWrap(size_t capacity, size_t index)
{
    return index + 1 >= capacity ? 0 : index + 1;
}

static inline void *elemAt(void *elems, size_t elemSize, size_t index)
{
    return ((char *)elems) + elemSize * index;
}

static inline bool needsRehash(const HashTable *hashTable)
{
    return hashTable->size * 100 >= hashTable->capacity * MAX_LOAD_FACTOR;
}

HashTable newHashTable(size_t elemSize)
{
    return newHashTableWithCapacity(elemSize, DEFAULT_HASH_TABLE_CAPACITY);
}

HashTable newHashTableWithCapacity(size_t elemSize, size_t capacity)
{
    capacity = nextPrime(capacity);
    void *elems = mallocOrDie(capacity * elemSize);
    HashCode *hashes = callocOrDie(capacity, sizeof(HashCode));
    return (HashTable){.elems = elems, .hashes = hashes, .capacity = capacity};
}

void freeHashTable(HashTable *hashTable)
{
    free(hashTable->elems);
    free(hashTable->hashes);
    hashTable->capacity = hashTable->size = 0;
}

static inline bool isOccupiedHash(HashCode hash)
{
    return hash & OCCUPIED_MASK;
}

bool isBucketOccupied(const HashTable *hashTable, size_t i)
{
    return isOccupiedHash(hashTable->hashes[i]);
}

static inline void rehashTable(HashTable *hashTable, size_t elemSize)
{
    size_t new_capacity = nextPrime(hashTable->capacity);
    if (new_capacity <= hashTable->capacity)
        new_capacity = hashTable->capacity * 2 + 1;
    void *new_elems = mallocOrDie(new_capacity * elemSize);
    HashCode *new_hashes = callocOrDie(new_capacity, elemSize);
    for (size_t i = 0, n = hashTable->capacity; i < n; ++i) {
        HashCode hash = hashTable->hashes[i];
        if (!isOccupiedHash(hash))
            continue;
        size_t index = modPrime(hash, new_capacity);
        while (isOccupiedHash(new_hashes[index]))
            index = incrementWrap(new_capacity, index);

        memcpy(elemAt(new_elems, elemSize, index),
               elemAt(hashTable->elems, elemSize, i),
               elemSize);
        new_hashes[index] = hash;
    }
    free(hashTable->hashes);
    free(hashTable->elems);
    hashTable->hashes = new_hashes;
    hashTable->elems = new_elems;
    hashTable->capacity = new_capacity;
}

bool insertInHashTable(HashTable *hashTable,
                       const void *elem,
                       HashCode hash,
                       size_t elemSize,
                       bool (*compare)(const void *, const void *))
{
    hash |= OCCUPIED_MASK;
    size_t index = modPrime(hash, hashTable->capacity);
    while (isBucketOccupied(hashTable, index)) {
        if (hashTable->hashes[index] == hash &&
            compare(elemAt(hashTable->elems, elemSize, index), elem))
            return false;
        index = incrementWrap(hashTable->capacity, index);
    }
    memcpy(elemAt(hashTable->elems, elemSize, index), elem, elemSize);
    hashTable->hashes[index] = hash;
    hashTable->size++;
    if (needsRehash(hashTable))
        rehashTable(hashTable, elemSize);
    return true;
}

void *findInHashTable(const HashTable *hashTable,
                      const void *elem,
                      HashCode hash,
                      size_t elemSize,
                      bool (*compare)(const void *, const void *))
{
    hash |= OCCUPIED_MASK;
    size_t index = modPrime(hash, hashTable->capacity);
    while (isBucketOccupied(hashTable, index)) {
        void *target_elem = elemAt(hashTable->elems, elemSize, index);
        if (hashTable->hashes[index] == hash && compare(target_elem, elem))
            return target_elem;
        index = incrementWrap(hashTable->capacity, index);
    }
    return NULL;
}

void enumerateHashTable(HashTable *table,
                        void *ctx,
                        bool (*with)(void *ctx, const void *elem),
                        size_t elemSize)
{
    for (size_t i = 0, n = table->capacity; i < n; ++i) {
        HashCode hash = table->hashes[i];
        if (!isOccupiedHash(hash))
            continue;
        if (!with(ctx, elemAt(table->elems, elemSize, i)))
            break;
    }
}

static inline size_t distanceInBytes(const void *from, const void *to)
{
    return (char *)to - (char *)from;
}

void removeFromHashTable(HashTable *hashTable, void *elem, size_t elemSize)
{
    assert(elem >= hashTable->elems);
    assert(elem < elemAt(hashTable->elems, elemSize, hashTable->capacity));
    size_t index = distanceInBytes(hashTable->elems, elem) / elemSize;
    assert(isBucketOccupied(hashTable, index));
    size_t next_index = incrementWrap(hashTable->capacity, index);
    while (isBucketOccupied(hashTable, next_index)) {
        HashCode next_hash = hashTable->hashes[next_index];
        size_t desired_index = modPrime(next_hash, hashTable->capacity);
        // If the next element is part of the collision chain, move it
        if (desired_index <= index || desired_index > next_index) {
            void *next_elem = elemAt(hashTable->elems, elemSize, next_index);
            memcpy(elem, next_elem, elemSize);
            hashTable->hashes[index] = next_hash;
            elem = next_elem;
            index = next_index;
        }
        next_index = incrementWrap(hashTable->capacity, next_index);
    }
    hashTable->hashes[index] = 0;
    hashTable->size--;
}

void clearHashTable(HashTable *hashTable)
{
    if (hashTable->size == 0)
        return;
    hashTable->size = 0;
    memset(hashTable->hashes, 0, sizeof(HashCode) * hashTable->capacity);
}
