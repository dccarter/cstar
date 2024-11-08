//
// Created by Carter Mbotho on 2024-11-01.
//

#include "core/alloc.h"
#include "driver/driver.h"

#include "context.h"

MirContext *mirContextCreate(struct CompilerDriver *cc)
{
    csAssert0(cc->mir == NULL);
    cc->mir = callocOrDie(sizeof(MirContext), 1);
    cc->mir->L = cc->L;
    cc->mir->pool = cc->pool;
    cc->mir->strings = cc->strings;
    cc->mir->types = cc->types;

    return cc->mir;
}
