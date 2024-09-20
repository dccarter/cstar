//
// Created by Carter Mbotho on 2024-09-10.
//

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct __SrcLoc {
    const char *file;
    uint64_t line;
} __SrcLoc;

void __trace_memory_custom(const char *tag, void *ptr, const __SrcLoc *loc)
{
    static FILE *output = NULL;
    if (output == NULL) {
        output = fopen(".mem-trace", "w");
        if (output == NULL) {
            perror("opening file for tracing failed\n");
            abort();
        }
    }
    if (ptr) {
        fprintf(output,
                " mem(%-5.5s: %p) @ %s:%" PRIu64 "\n",
                tag,
                ptr,
                loc->file,
                loc->line);
    }
    else {
        fflush(output);
    }
}
