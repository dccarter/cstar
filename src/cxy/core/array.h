
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Dynamically-growing array implementation.
 * In debug mode, assertions check that the array is not used with the wrong
 * element key.
 */

#define pushOnDynArray(array, ...)                                             \
    pushOnDynArrayExplicit(array, (__VA_ARGS__), sizeof(*(__VA_ARGS__)))
#define new_dyn_array_from_data(begin, size)                                   \
    newDynArrayFromDataExplicit(begin, size, sizeof(*(begin)));

typedef struct DynArray {
    size_t size;
    size_t elemSize;
    size_t capacity;
    void *elems;
} DynArray;

DynArray newDynArray(size_t elemSize);
DynArray newDynArrayWithSize(size_t elemSize, size_t size);
DynArray newDynArrayFromDataExplicit(void *, size_t, size_t);
void pushOnDynArrayExplicit(DynArray *, const void *, size_t);
void copyDynArray(DynArray *dst, const DynArray *src);
static inline void pushStringOnDynArray(DynArray *array, const char *str)
{
    pushOnDynArray(array, &str);
}
void resizeDynArray(DynArray *, size_t);
void clearDynArray(DynArray *);
void freeDynArray(DynArray *);
void *popDynArray(DynArray *);
#define dynArrayAt(T, arr, idx) ((T)(arr)->elems)[(idx)]
#define dynArrayEmpty(arr) ((arr)->size == 0)
#define dynArrayBack(T, arr) dynArrayAt(T, (arr), (arr)->size - 1)
#define dynArrayFor(NAME, T, arr)                                              \
    u64 LINE_VAR(i) = 0;                                                       \
    for (T *NAME = &((arr)->elems[LINE_VAR(i)]); LINE_VAR(i) < (arr)->size;    \
         NAME = &((arr)->elems[LINE_VAR(i)]), LINE_VAR(i)++)
#ifdef __cplusplus
}
#endif