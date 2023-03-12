#include "core/array.h"
#include "core/alloc.h"

#include <string.h>
#include <assert.h>

#define DEFAULT_CAPACITY 4

static inline DynArray newDynArrayWithCapacity(size_t elemSize, size_t capacity) {
    void* elems = mallocOrDie(elemSize * capacity);
    return (DynArray) {
        .capacity = capacity,
        .elemSize = elemSize,
        .elems = elems
    };
}

DynArray newDynArray(size_t elemSize) {
    return newDynArrayWithCapacity(elemSize, DEFAULT_CAPACITY);
}

DynArray newDynArrayWithSize(size_t elemSize, size_t size) {
    DynArray array = newDynArrayWithCapacity(elemSize, size);
    array.size = size;
    return array;
}

DynArray newDynArrayFromDataExplicit(void* begin, size_t size, size_t elemSize) {
    DynArray array = newDynArrayWithSize(elemSize, size);
    memcpy(array.elems, begin, size * elemSize);
    return array;
}

static void growDynArray(DynArray* array, size_t capacity) {
    size_t double_capacity = array->capacity * 2;
    array->capacity = double_capacity > capacity ? double_capacity : capacity;
    array->elems = reallocOrDie(array->elems, array->elemSize * array->capacity);
}

void pushOnDynArrayExplicit(DynArray* array, const void* elem, size_t elemSize) {
    assert(elemSize == array->elemSize);
    if (array->size >= array->capacity)
        growDynArray(array, array->size + 1);
    memcpy((char*)array->elems + array->elemSize * array->size, elem, array->elemSize);
    array->size++;
}

void resizeDynArrayExplicit(DynArray* array, size_t size) {
    if (size > array->capacity)
        growDynArray(array, size);
    array->size = size;
}

void freeDynArray(DynArray* array) {
    free(array->elems);
    memset(array, 0, sizeof(DynArray));
}
