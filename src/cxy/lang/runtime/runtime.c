//
// Created by Carter Mbotho on 2023-07-10.
//

#include "epilogue.h"

static int __cxy_enum_name_compare(const void *lhs, const void *rhs)
{
    return (int)(((__cxy_enum_name_t *)lhs)->value -
                 ((__cxy_enum_name_t *)rhs)->value);
}

void *__cxy_default_realloc(void *ptr, u64 size, void (*destructor)(void *))
{
    if (ptr == NULL)
        return __cxy_default_alloc(size, destructor);

    __cxy_memory_hdr_t *hdr = CXY_MEMORY_HEADER(ptr);
    if (hdr->magic == CXY_MEMORY_MAGIC(HEAP)) {
        if (hdr->refs == 1) {
            if (hdr->destructor)
                hdr->destructor(ptr);
            hdr = realloc(hdr, size + CXY_MEMORY_HEADER_SIZE);
            return CXY_MEMORY_POINTER(hdr);
        }
        else {
            --hdr->refs;
        }
    }

    return __cxy_default_alloc(size, destructor);
}

void __cxy_default_dealloc(void *ptr)
{
    if (ptr) {
        __cxy_memory_hdr_t *hdr = CXY_MEMORY_HEADER(ptr);
        if (hdr->magic == CXY_MEMORY_MAGIC(HEAP) && hdr->refs) {
            if (hdr->refs == 1) {
                if (hdr->destructor)
                    hdr->destructor(ptr);
                memset(hdr, 0, sizeof(*hdr));
                free(hdr);
            }
            else
                hdr->refs--;
        }
    }
}

void *__cxy_alloc_slice_(u64 count, u64 size, void (*destructor)(void *))
{
    // destructor should be responsible for deleting the individual elements
    __cxy_slice_t *slice =
        __cxy_alloc(sizeof(__cxy_slice_t) + (count * size), destructor);
    slice->len = count;
    slice->data = slice->p;
    return slice;
}

attr(noreturn) attr(format, printf, 1, 2) void cxyAbort(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    abort();
}

void __cxy_string_builder_grow(__cxy_string_builder_t *sb, u64 size)
{
    if (sb->data == NULL) {
        sb->data = __cxy_alloc(size + 1, nullptr);
        sb->capacity = size;
    }
    else if (size > (sb->capacity - sb->size)) {
        while (sb->capacity < sb->size + size) {
            sb->capacity <<= 1;
        }
        sb->data = __cxy_realloc(sb->data, sb->capacity + 1, nullptr);
    }
}

__cxy_string_builder_t *__cxy_string_builder_new()
{
    __cxy_string_builder_t *sb = __cxy_calloc(
        1, sizeof(__cxy_string_builder_t), __cxy_string_builder_delete_fwd);
    __cxy_string_builder_init(sb);
    return sb;
}

void __cxy_string_builder_append_cstr0(__cxy_string_builder_t *sb,
                                       const char *cstr,
                                       u64 len)
{
    if (cstr) {
        __cxy_string_builder_grow(sb, len);
        memmove(&sb->data[sb->size], cstr, len);
        sb->size += len;
        sb->data[sb->size] = '\0';
    }
    else
        __cxy_string_builder_append_cstr0(sb, "null", 4);
}

int __cxy_builtins_binary_search(const void *arr,
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

const char *__cxy_enum_find_name(const __cxy_enum_name_t *names,
                                 u64 count,
                                 u64 value)
{
    __cxy_enum_name_t name = {.value = value};
    int index = __cxy_builtins_binary_search(
        names, count, &name, sizeof(name), __cxy_enum_name_compare);
    if (index >= 0)
        return names[index].name;

    return "(Unknown)";
}
