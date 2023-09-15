//
// Created by Carter on 2023-09-14.
//
//
// Created by Carter Mbotho on 2023-07-10.
//

#include "prologue.h"

static int CXY__enum_name_compare(const void *lhs, const void *rhs)
{
    return (int)(((CXY__enum_name_t *)lhs)->value -
                 ((CXY__enum_name_t *)rhs)->value);
}

void *CXY__default_realloc(void *ptr, u64 size, void (*destructor)(void *))
{
    if (ptr == NULL)
        return CXY__default_alloc(size, destructor);

    CXY__memory_hdr_t *hdr = CXY_MEMORY_HEADER(ptr);
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

    return CXY__default_alloc(size, destructor);
}

void CXY__default_dealloc(void *ptr)
{
    if (ptr) {
        CXY__memory_hdr_t *hdr = CXY_MEMORY_HEADER(ptr);
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

void *CXY__alloc_slice_(u64 count, u64 size, void (*destructor)(void *))
{
    // destructor should be responsible for deleting the individual elements
    CXY__slice_t *slice =
        CXY__alloc(sizeof(CXY__slice_t) + (count * size), destructor);
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

void CXY__builtins_string_builder_grow(CXY__builtins_string_builder_t *sb,
                                       u64 size)
{
    if (sb->data == NULL) {
        sb->data = CXY__alloc(size + 1, nullptr);
        sb->capacity = size;
    }
    else if (size > (sb->capacity - sb->size)) {
        while (sb->capacity < sb->size + size) {
            sb->capacity <<= 1;
        }
        sb->data = CXY__realloc(sb->data, sb->capacity + 1, nullptr);
    }
}

CXY__builtins_string_builder_t *CXY__builtins_string_builder_new()
{
    CXY__builtins_string_builder_t *sb =
        CXY__calloc(1,
                    sizeof(CXY__builtins_string_builder_t),
                    CXY__builtins_string_builder_delete_fwd);
    CXY__builtins_string_builder_init(sb);
    return sb;
}

void CXY__builtins_string_builder_append_cstr0(
    CXY__builtins_string_builder_t *sb, const char *cstr, u64 len)
{
    if (cstr) {
        CXY__builtins_string_builder_grow(sb, len);
        memmove(&sb->data[sb->size], cstr, len);
        sb->size += len;
        sb->data[sb->size] = '\0';
    }
    else
        CXY__builtins_string_builder_append_cstr0(sb, "null", 4);
}

int CXY__builtins_binary_search(const void *arr,
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

const char *CXY__enum_find_name(const CXY__enum_name_t *names,
                                u64 count,
                                u64 value)
{
    CXY__enum_name_t name = {.value = value};
    int index = CXY__builtins_binary_search(
        names, count, &name, sizeof(name), CXY__enum_name_compare);
    if (index >= 0)
        return names[index].name;

    return "(Unknown)";
}
