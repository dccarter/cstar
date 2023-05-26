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
    Log log = newLog(&state);
    bool status = true;

    if (!parse_options(&argc, argv, &driver.options, &log)) {
        status = false;
        goto exit;
    }
    initCompilerDriver(&driver, &log);

    for (int i = 1; i < argc && status; ++i)
        status &= compileSource(argv[i], &driver);

exit:
    writeFormatState(&state, stderr);
    freeFormatState(&state);
    freeLog(&log);
    return status ? EXIT_SUCCESS : EXIT_FAILURE;
}
