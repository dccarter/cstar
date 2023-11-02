/*
Copyright (c) 2021 Scott Lembcke

Permission is hereby granted, free of charge, to any person obtaining a copy
                                                  of this software and
associated documentation files (the "Software"), to deal in the Software without
    restriction, including without limitation the rights to use, copy, modify,
    merge, publish, distribute, sublicense, and/or sell copies of the Software,
and to permit persons to whom the Software is furnished to do so, subject to the
        following conditions:

    The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
            */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __CXY_BUILD__
#include "../prologue.h"
#endif

// Coroutine type.
typedef struct tina tina;
// Coroutine body function prototype.
typedef void *tina_func(tina *coro, void *value);

struct tina {
    // Body function used by the coroutine. (readonly)
    tina_func *body;
    // Used by schedulers
    struct tina *next;
    // User defined context pointer passed to tina_init(). (optional)
    void *user_data;
    // User defined name. (optional)
    const char *name;
    // Pointer to the coroutine's memory buffer. (readonly)
    void *buffer;
    // Size of the buffer. (readonly)
    size_t size;
    // Has the coroutine's body function exited? (readonly)
    bool completed;
    // the coroutine ID
    u64 cid;
    // Private:
    tina *_caller;
    void *_stack_pointer;
    // Stack canary values at the start and end of the buffer.
    const uint32_t *_canary_end;
    uint32_t _canary;
};

tina *tina_init(void *buffer, size_t size, tina_func *body, void *user_data);
void *tina_resume(tina *coro, void *value);
void *tina_yield(tina *coro, void *value);
extern const tina TINA_EMPTY;
void *tina_swap(tina *from, tina *to, void *value);

tina *tina_running();

#ifdef __cplusplus
}
#endif
