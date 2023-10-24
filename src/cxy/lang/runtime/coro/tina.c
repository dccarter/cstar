//
// Created by Carter Mbotho on 2023-07-13.
//

#ifndef __CXY_BUILD__
#include "tina.h"
#endif

#ifndef TINA_MIN_CORO_STACK_SIZE
#define TINA_MIN_CORO_STACK_SIZE (4 * 1024)
#endif

#ifndef TINA_NO_CRT
#include <stdlib.h>

#ifndef _TINA_ALLOC
#define _TINA_ALLOC malloc
#endif

#ifndef _TINA_ASSERT
#include <stdio.h>
#define _TINA_ASSERT(_COND_, _MESSAGE_)                                        \
    {                                                                          \
        if (!(_COND_)) {                                                       \
            fprintf(stderr, _MESSAGE_ "\n");                                   \
            abort();                                                           \
        }                                                                      \
    }
#endif
#else
#define _TINA_ASSERT(_COND_, _MESSAGE_)
#endif

#if _MSC_VER
// Negation of unsigned integers is well defined. Warning is not helpful.
#pragma warning(disable : 4146)
#endif

// Alignment to use for all types. (MSVC doesn't provide stdalign.h -_-)
#define _TINA_MAX_ALIGN ((size_t)16)

const tina TINA_EMPTY = {
    .body = NULL,
    .user_data = NULL,
    .name = "TINA_EMPTY",
    .buffer = NULL,
    .size = 0,
    .completed = false,
    ._caller = NULL,
    ._stack_pointer = NULL,
    ._canary_end = &TINA_EMPTY._canary,
    ._canary = 0x54494E41ul,
};

struct {
    tina *co;
    bool update;
} __tina_running = {NULL, false};

// Symbols for the assembly functions.
// These are either defined as inline assembly (GCC/Clang) of binary blobs
// (MSVC).
#if __WIN64__ || _WIN64
extern const uint64_t _tina_swap[];
extern const uint64_t _tina_init_stack[];
#else
// Avoid the MSVC hack unless necessary!
extern void *_tina_swap(void **sp_from, void **sp_to, void *value);
extern tina *_tina_init_stack(tina *coro,
                              tina_func *body,
                              void **sp_loc,
                              void *sp);
#endif

tina *tina_init(void *buffer, size_t size, tina_func *body, void *user_data)
{
    _TINA_ASSERT(size >= TINA_MIN_CORO_STACK_SIZE,
                 "Tina Warning: Small stacks tend to not work on modern OSes. "
                 "(Feel free to disable this if you have your reasons)");
#ifndef TINA_NO_CRT
    if (buffer == NULL)
        buffer = _TINA_ALLOC(size);
#endif

    // Make sure 'buffer' is properly aligned.
    uintptr_t aligned = -(-(uintptr_t)buffer & -_TINA_MAX_ALIGN);
    size -= aligned - (uintptr_t)buffer;
    // Find the stack end, saving room for the canary value.
    void *stack_end = (uint8_t *)buffer + size - sizeof(TINA_EMPTY._canary);
    *(uint32_t *)stack_end = TINA_EMPTY._canary;

    tina *coro = (tina *)aligned;
    (*coro) = (tina){
        .body = body,
        .user_data = user_data,
        .name = "<no name>",
        .buffer = buffer,
        .size = size,
        .completed = false,
        ._caller = NULL,
        ._stack_pointer = NULL,
        ._canary_end = (uint32_t *)stack_end,
        ._canary = TINA_EMPTY._canary,
    };

    // Empty coroutine for the init function to use for a return location.
    tina dummy = TINA_EMPTY;
    coro->_caller = &dummy;

    typedef tina *init_func(
        tina * coro, tina_func * body, void **sp_loc, void *sp);
    __tina_running.update = false;
    return ((init_func *)_tina_init_stack)(
        coro, body, &dummy._stack_pointer, stack_end);
}

void _tina_context(
    tina *coro); // Can this be static and still referenced by the inline asm?!
void _tina_context(tina *coro)
{
    // Yield back to the _tina_init_stack() call, and return the coroutine.
    void *value = tina_yield(coro, coro);
    // Call the body function with the first value.
    value = coro->body(coro, value);
    // body() has exited, and the coroutine is completed.
    coro->completed = true;
    // Yield the final return value back to the calling thread.
    _TINA_ASSERT(coro->_caller,
                 "Tina Error: You must not return from a symmetric coroutine "
                 "body function.");
    tina_yield(coro, value);

    _TINA_ASSERT(
        false,
        "Tina Error: You cannot resume a coroutine after it has finished.");
#ifndef TINA_NO_CRT
    abort(); // Crash predictably if assertions are disabled.
#endif
}

void *tina_swap(tina *from, tina *to, void *value)
{
    _TINA_ASSERT(from->_canary == TINA_EMPTY._canary,
                 "Tina Error: Bad canary value. Coroutine has likely had a "
                 "stack overflow.");
    _TINA_ASSERT(*from->_canary_end == TINA_EMPTY._canary,
                 "Tina Error: Bad canary value. Coroutine has likely had a "
                 "stack underflow.");
    __tina_running.co = __tina_running.update ? to : __tina_running.co;
    __tina_running.update = true;

    typedef void *swap(void **sp_from, void **sp_to, void *value);
    return ((swap *)_tina_swap)(
        &from->_stack_pointer, &to->_stack_pointer, value);
}

void *tina_resume(tina *coro, void *value)
{
    _TINA_ASSERT(!coro->_caller,
                 "Tina Error: tina_resume() called on a coroutine that hasn't "
                 "yielded yet.");
    tina dummy = TINA_EMPTY;
    coro->_caller = &dummy;
    return tina_swap(&dummy, coro, value);
}

void *tina_yield(tina *coro, void *value)
{
    _TINA_ASSERT(
        coro->_caller,
        "Tina Error: tina_yield() called on a coroutine that wasn't resumed.");
    tina *caller = coro->_caller;
    coro->_caller = NULL;
    return tina_swap(coro, caller, value);
}

tina *tina_running() { return __tina_running.co; }

#if __APPLE__
#define _TINA_SYMBOL(sym) "_" #sym
#else
#define _TINA_SYMBOL(sym) #sym
#endif

#if __ARM_EABI__ && __GNUC__
// TODO: Is this an appropriate macro check for a 32 bit ARM ABI?
// TODO: Only tested on RPi3.

// Since the 32 bit ARM version is by far the shortest, I'll document this one.
// The other variations are basically the same structurally.

// _tina_init_stack() sets up the stack and initial execution of the coroutine.
asm("_tina_init_stack:");
// First things first, save the registers protected by the ABI
asm("  push {r4-r11, lr}");
asm("  vpush {q4-q7}");
// Now store the stack pointer in the couroutine.
// _tina_context() will call tina_yield() to restore the stack and registers
// later.
asm("  str sp, [r2]");
// Align the stack top to 16 bytes as requested by the ABI and set it to the
// stack pointer.
asm("  and r3, r3, #~0xF");
asm("  mov sp, r3");
// Finally, tail call into _tina_context.
// By setting the caller to null, debuggers will show _tina_context() as a base
// stack frame.
asm("  mov lr, #0");
asm("  b _tina_context");

// https://static.docs.arm.com/ihi0042/g/aapcs32.pdf
// _tina_swap() is responsible for swapping out the registers and stack pointer.
asm("_tina_swap:");
// Like above, save the ABI protected registers and save the stack pointer.
asm("  push {r4-r11, lr}");
asm("  vpush {q4-q7}");
// Save stack pointer for the old coroutine, and load the new one.
asm("  str sp, [r0]");
asm("  ldr sp, [r1]");
// Restore the new coroutine's protected registers.
asm("  vpop {q4-q7}");
asm("  pop {r4-r11, lr}");
// Move the 'value' parameter to the return value register.
asm("  mov r0, r2");
// And perform a normal return instruction.
// This will return from tina_yield() in the new coroutine.
asm("  bx lr");
#elif __amd64__ && (__unix__ || __APPLE__)
#define ARG0 "rdi"
#define ARG1 "rsi"
#define ARG2 "rdx"
#define ARG3 "rcx"
#define RET "rax"

asm(".intel_syntax noprefix");

asm(_TINA_SYMBOL(_tina_init_stack:));
asm("  push rbp");
asm("  push rbx");
asm("  push r12");
asm("  push r13");
asm("  push r14");
asm("  push r15");
asm("  mov [" ARG2 "], rsp");
asm("  and " ARG3 ", ~0xF");
asm("  mov rsp, " ARG3);
asm("  push 0");
asm("  jmp " _TINA_SYMBOL(_tina_context));

// https://software.intel.com/sites/default/files/article/402129/mpx-linux64-abi.pdf
asm(_TINA_SYMBOL(_tina_swap:));
asm("  push rbp");
asm("  push rbx");
asm("  push r12");
asm("  push r13");
asm("  push r14");
asm("  push r15");
asm("  mov [" ARG0 "], rsp");
asm("  mov rsp, [" ARG1 "]");
asm("  pop r15");
asm("  pop r14");
asm("  pop r13");
asm("  pop r12");
asm("  pop rbx");
asm("  pop rbp");
asm("  mov " RET ", " ARG2);
asm("  ret");

asm(".att_syntax");
#elif __WIN64__ || _WIN64
// MSVC doesn't allow inline assembly, assemble to binary blob then.

#if __GNUC__
#define TINA_SECTION_ATTRIBUTE __attribute__((section(".text#")))
#elif _MSC_VER
#pragma section(".text")
#define TINA_SECTION_ATTRIBUTE __declspec(allocate(".text"))
#else
#error Unknown/untested compiler for Win64.
#endif

// Assembled and dumped from win64-init.S
TINA_SECTION_ATTRIBUTE
const uint64_t _tina_init_stack[] = {
    0x5541544157565355, 0x2534ff6557415641,      0x2534ff6500000008,
    0x2534ff6500000010, 0xa0ec814800001478,      0x9024b4290f000000,
    0x8024bc290f000000, 0x2444290f44000000,      0x4460244c290f4470,
    0x290f44502454290f, 0x2464290f4440245c,      0x4420246c290f4430,
    0x290f44102474290f, 0xe18349208949243c,      0x0c894865cc894cf0,
    0x8948650000147825, 0x4c6500000010250c,      0x6a00000008250c89,
    0xb8489020ec834800, (uint64_t)_tina_context, 0x909090909090e0ff,
    0x9090909090909090,
};

// Assembled and dumped from win64-swap.S
TINA_SECTION_ATTRIBUTE
const uint64_t _tina_swap[] = {
    0x5541544157565355, 0x2534ff6557415641, 0x2534ff6500000008,
    0x2534ff6500000010, 0xa0ec814800001478, 0x9024b4290f000000,
    0x8024bc290f000000, 0x2444290f44000000, 0x4460244c290f4470,
    0x290f44502454290f, 0x2464290f4440245c, 0x4420246c290f4430,
    0x290f44102474290f, 0x228b48218948243c, 0x0000009024b4280f,
    0x0000008024bc280f, 0x0f44702444280f44, 0x54280f4460244c28,
    0x40245c280f445024, 0x0f44302464280f44, 0x74280f4420246c28,
    0x48243c280f441024, 0x8f65000000a0c481, 0x8f65000014782504,
    0x8f65000000102504, 0x5f41000000082504, 0x5e5f5c415d415e41,
    0x9090c3c0894c5d5b,
};
#elif __aarch64__ && __GNUC__
asm(_TINA_SYMBOL(_tina_init_stack:));
asm("  sub sp, sp, 0xA0");
asm("  stp x19, x20, [sp, 0x00]");
asm("  stp x21, x22, [sp, 0x10]");
asm("  stp x23, x24, [sp, 0x20]");
asm("  stp x25, x26, [sp, 0x30]");
asm("  stp x27, x28, [sp, 0x40]");
asm("  stp x29, x30, [sp, 0x50]");
asm("  stp d8 , d9 , [sp, 0x60]");
asm("  stp d10, d11, [sp, 0x70]");
asm("  stp d12, d13, [sp, 0x80]");
asm("  stp d14, d15, [sp, 0x90]");
asm("  mov x4, sp");
asm("  str x4, [x2]");
asm("  and x3, x3, #~0xF");
asm("  mov sp, x3");
asm("  mov lr, #0");
asm("  b " _TINA_SYMBOL(_tina_context));

asm(_TINA_SYMBOL(_tina_swap:));
asm("  sub sp, sp, 0xA0");
asm("  stp x19, x20, [sp, 0x00]");
asm("  stp x21, x22, [sp, 0x10]");
asm("  stp x23, x24, [sp, 0x20]");
asm("  stp x25, x26, [sp, 0x30]");
asm("  stp x27, x28, [sp, 0x40]");
asm("  stp x29, x30, [sp, 0x50]");
asm("  stp d8 , d9 , [sp, 0x60]");
asm("  stp d10, d11, [sp, 0x70]");
asm("  stp d12, d13, [sp, 0x80]");
asm("  stp d14, d15, [sp, 0x90]");
asm("  mov x3, sp");
asm("  str x3, [x0]");
asm("  ldr x3, [x1]");
asm("  mov sp, x3");
asm("  ldp x19, x20, [sp, 0x00]");
asm("  ldp x21, x22, [sp, 0x10]");
asm("  ldp x23, x24, [sp, 0x20]");
asm("  ldp x25, x26, [sp, 0x30]");
asm("  ldp x27, x28, [sp, 0x40]");
asm("  ldp x29, x30, [sp, 0x50]");
asm("  ldp d8 , d9 , [sp, 0x60]");
asm("  ldp d10, d11, [sp, 0x70]");
asm("  ldp d12, d13, [sp, 0x80]");
asm("  ldp d14, d15, [sp, 0x90]");
asm("  add sp, sp, 0xA0");
asm("  mov x0, x2");
asm("  ret");
#endif
