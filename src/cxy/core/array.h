
#pragma once

#include <stddef.h>

/*
 * Dynamically-growing array implementation.
 * In debug mode, assertions check that the array is not used with the wrong element type.
 */

#define pushOnDynArray(array, ...) \
    pushOnDynArrayExplicit(array, (__VA_ARGS__), sizeof(*(__VA_ARGS__)))
#define new_dyn_array_from_data(begin, size) \
    newDynArrayFromDataExplicit(begin, size, sizeof(*(begin)));

typedef struct DynArray {
    size_t size;
    size_t elemSize;
    size_t capacity;
    void* elems;
} DynArray;

DynArray newDynArray(size_t elemSize);
DynArray newDynArrayWithSize(size_t elemSize, size_t size);
DynArray newDynArrayFromDataExplicit(void*, size_t, size_t);
void pushOnDynArrayExplicit(DynArray*, const void*, size_t);
void resizeDynArray(DynArray*, size_t);
void clearDynArray(DynArray*);
void freeDynArray(DynArray*);

