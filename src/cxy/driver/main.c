#include "driver/options.h"
#include "driver/driver.h"
#include "core/log.h"
#include "core/utils.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    FormatState state = newFormatState("    ", !isColorSupported(stderr));
    Log log = newLog(&state);
    bool status = true;

    Options options = default_options;
    if (!parse_options(&argc, argv, &options, &log)) {
        status = false;
        goto exit;
    }

    for (int i = 1; i < argc && status; ++i)
        status &= compileFile(argv[i], &options, &log);

exit:
    writeFormatState(&state, stderr);
    freeFormatState(&state);
    freeLog(&log);
    return status ? EXIT_SUCCESS : EXIT_FAILURE;
}
