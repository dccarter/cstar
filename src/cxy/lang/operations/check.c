//
// Created by Carter Mbotho on 2023-07-14.
//

#include "lang/operations.h"

#include "lang/ttable.h"

#define ERROR_TYPE(CTX) makeErrorType((CTX)->types)

typedef struct {
    Log *L;
    TypeTable *types;
} TypingContext;