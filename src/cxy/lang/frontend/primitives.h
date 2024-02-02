//
// Created by Carter Mbotho on 2024-02-01.
//

#pragma once

#include "primitives.def"

typedef enum {
#define f(name, ...) prt##name,
    PRIM_TYPE_LIST(f)
#undef f
        prtCOUNT
} PrtId;
