#include "core/log.h"
#include "core/utils.h"
#include "driver/driver.h"
#include "driver/options.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    FormatState state = newFormatState("    ", !isColorSupported(stderr));
    CompilerDriver driver = {0};
    Log log = newLog(NULL, NULL);
    MemPool pool = newMemPool();
    StrPool strings = newStrPool(&pool);

    bool status = true;

    if (!parseCommandLineOptions(
            &argc, argv, &strings, &driver.options, &log)) {
        status = false;
        goto exit;
    }

    if (!initCompilerDriver(&driver, &pool, &strings, &log)) {
        status = false;
        goto exit;
    }

    for (int i = 1; i < argc && status; ++i)
        status &= compileFile(argv[i], &driver);

exit:
    writeFormatState(&state, stderr);
    freeFormatState(&state);
    freeLog(&log);
    return status ? EXIT_SUCCESS : EXIT_FAILURE;
}
