#include "core/utils.h"
#include "core/alloc.h"

#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>

#ifdef WIN32
#define isatty _isatty
#define fileno _fileno
#include <io.h>
#else
#include <unistd.h>
#endif

#ifndef NDEBUG
#define CHUNK_SIZE 4
#else
#define CHUNK_SIZE 4096
#endif

static size_t convertStrToCharOrd(const char* ptr, int base, char* res) {
    char* next = NULL;
    unsigned int ord = strtoul(ptr, &next, base);
    *res = ord;
    return ord <= 255 && !errno ? next - ptr : 0;
}

size_t convertEscapeSeq(const char* ptr, size_t n, char* res) {
    if (n == 0) return 0;
    if (ptr[0] == '\\') {
        if (n <= 1)
            return 0;
        switch (ptr[1]) {
            case 'n': *res = '\n'; return n >= 2 ? 2 : 0;
            case 't': *res = '\t'; return n >= 2 ? 2 : 0;
            case 'v': *res = '\v'; return n >= 2 ? 2 : 0;
            case 'r': *res = '\r'; return n >= 2 ? 2 : 0;
            case 'a': *res = '\a'; return n >= 2 ? 2 : 0;
            case 'b': *res = '\b'; return n >= 2 ? 2 : 0;
            case '$': *res = '$';  return n >= 2 ? 2 : 0;
            case 'x':
                if (n <= 2)
                    return 0;
                return convertStrToCharOrd(ptr + 2, 16, res);
            default:
                if (isdigit(ptr[1]))
                    return convertStrToCharOrd(ptr + 1, 8, res);
                return 0;
        }
    }
    *res = ptr[0];
    return n >= 1 ? 1 : 0;
}

bool isColorSupported(FILE* file) {
    return isatty(fileno(file));
}

char* readFile(const char* fileName, size_t* file_size) {
    FILE* file = fopen(fileName, "rb");
    if (!file)
        return NULL;
    size_t chunk_size = CHUNK_SIZE;
    char* file_data = NULL;
    *file_size = 0;
    while (true) {
        if (ferror(file)) {
            fclose(file);
            free(file_data);
            return NULL;
        }
        file_data = reallocOrDie(file_data, *file_size + chunk_size);
        size_t read_count = fread(file_data + *file_size, 1, chunk_size, file);
        *file_size += read_count;
        if (read_count < chunk_size)
            break;
        chunk_size *= 2;
    }
    fclose(file);

    // Add terminator
    file_data = reallocOrDie(file_data, *file_size + 1);
    file_data[*file_size] = 0;
    return file_data;
}

void cynAbort(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    abort();
}
