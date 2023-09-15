/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-22
 */

#include "core/args.h"
#include "core/format.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define AMALGAMATE_DIR 1
#define AMALGAMATE_FNAME 2
#define AMALGAMATE_VARNAME 3
#define AMALGAMATE_FIRST_FILE 4
#define AMALGAMATE_MIN_ARGS 5

int main(int argc, char *argv[])
{
    if (argc < AMALGAMATE_MIN_ARGS)
        return EXIT_FAILURE;

    char path[512];
    {
        snprintf(path,
                 sizeof(path),
                 "%s/%s.h",
                 argv[AMALGAMATE_DIR],
                 argv[AMALGAMATE_FNAME]);

        FILE *output = fopen(path, "w");
        if (output == NULL) {
            fprintf(stdout,
                    "opening output file '%s' failed - %s\n",
                    path,
                    strerror(errno));
            return EXIT_FAILURE;
        }

        fprintf(output,
                "extern const char *CXY_%s_SOURCE;\n",
                argv[AMALGAMATE_VARNAME]);
        fprintf(output,
                "extern unsigned long CXY_%s_SOURCE_SIZE;\n",
                argv[AMALGAMATE_VARNAME]);
        fprintf(output,
                "extern unsigned long CXY_%s_SOURCE_MTIME;\n",
                argv[AMALGAMATE_VARNAME]);
        fclose(output);
    }

    snprintf(path,
             sizeof(path),
             "%s/%s.c",
             argv[AMALGAMATE_DIR],
             argv[AMALGAMATE_FNAME]);
    FILE *output = fopen(path, "w");
    if (output == NULL) {
        fprintf(stdout,
                "opening output file '%s' failed - %s\n",
                path,
                strerror(errno));
        return EXIT_FAILURE;
    }

    fprintf(output, "const char *CXY_%s_SOURCE = \"", argv[AMALGAMATE_VARNAME]);
    size_t size = 0;
    u64 modified = 0;
    for (u64 j = AMALGAMATE_FIRST_FILE; j < argc; j++) {
        size_t bytes;
        char *data = readFile(argv[j], &bytes);
        if (data == NULL) {
            fprintf(stderr,
                    "reading file '%s' failed - %s\n",
                    argv[j],
                    strerror(errno));
            fclose(output);
            return EXIT_FAILURE;
        }
        struct stat st;
        stat(argv[j], &st);
        u64 mtime = timespecToMicroSeconds(&st.st_mtimespec);
        modified = MAX(modified, mtime);

        size += bytes;
        size += fprintf(output, "\\n/*---------%s----------*/\\n", argv[j]) - 2;
        for (u64 i = 0; i < bytes; i++) {
            if (data[i] == '"') {
                fputc('\\', output);
                fputc('"', output);
            }
            else if (data[i] == '\n') {
                fputc('\\', output);
                fputc('n', output);
            }
            else if (data[i] == '\\') {
                fputc('\\', output);
                fputc('\\', output);
            }
            else
                fputc(data[i], output);
        }
        size += fprintf(output, "\\n\\n") - 2;
        free(data);
    }
    fputs("\";\n", output);
    fprintf(output,
            "unsigned long CXY_%s_SOURCE_SIZE = %zu;\n",
            argv[AMALGAMATE_VARNAME],
            size);
    fprintf(output,
            "unsigned long CXY_%s_SOURCE_MTIME = %llu;\n",
            argv[AMALGAMATE_VARNAME],
            modified);
    fclose(output);

    return EXIT_SUCCESS;
}
