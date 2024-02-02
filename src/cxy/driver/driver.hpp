//
// Created by Carter Mbotho on 2024-02-02.
//

#pragma once

#include <vector>

extern "C" {
#include "config.h"
#include <core/mempool.h>
#include <core/strpool.h>
}

namespace cxy {

class Compiler {
    CompilerConfig config{};
    Log L;
    MemPool generalMemPool;
    MemPool stringsMemPool;
    StrPool strings;

public:
    Compiler();

    bool compile(cstring path);
};

} // namespace cxy
