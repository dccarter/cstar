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

#include <stdlib.h>

int main(int argc, char *argv[])
{
    size_t bytes;
    if (argc < 4)
        return EXIT_FAILURE;

    char *data = readFile(argv[1], &bytes);
    if (data == NULL) {
        perror("reading file failed");
        return EXIT_FAILURE;
    }

    FILE *output = fopen(argv[2], "w");
    if (output == NULL) {
        perror("opening output file failed");
        free(data);
        return EXIT_FAILURE;
    }
    fprintf(output, "#define %s_CODE_SIZE %zu\n", argv[3], bytes);
    fprintf(output, "#define %s_CODE \"", argv[3]);

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
    fputs("\"", output);
    fclose(output);
    free(data);

    return EXIT_SUCCESS;
}