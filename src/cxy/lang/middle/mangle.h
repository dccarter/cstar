//
// Created by Carter Mbotho on 2024-04-30.
//

#pragma once

#include <lang/frontend/ast.h>

#ifdef __cplusplus
extern "C" {
#endif

cstring makeMangledName(struct StrPool *strings,
                        cstring name,
                        const Type **types,
                        u64 count,
                        bool isConst);

#ifdef __cplusplus
}
#endif
