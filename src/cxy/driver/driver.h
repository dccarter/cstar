
#pragma once

#include <stdbool.h>

typedef struct Log Log;
typedef struct Options Options;

bool compileFile(const char *fileName, const Options *options, Log *log);
