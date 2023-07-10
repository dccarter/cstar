//
// Created by Carter Mbotho on 2023-07-10.
//

#pragma once

#include <lang/ast.h>

u64 addClosureCapture(ClosureCapture *set, cstring name, const Type *type);

u64 getOrderedCapture(ClosureCapture *set,
                      const Type **capture,
                      const char **names,
                      u64 count);
