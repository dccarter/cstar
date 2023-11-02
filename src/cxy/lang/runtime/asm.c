#include "prologue.h"

typedef struct _st_jmp_buf {
#if defined(__APPLE__)
#if (defined(__x86_64) || defined(__x86_64__))
    long __jumpbuf[8];
#elif defined(__aarch64__)
    long __jumpbuf[22];
#else
#error "Unsupported architecture"
#endif
#elif defined(linux)
#if defined(__i386__)
    long __jmpbuf[6];
#elif defined(__amd64) || defined(__amd64__)
    long __jmpbuf[8];
#elif defined(__aarch64__)
    long __jmpbuf[22];
#elif defined(__mips__) || defined(__mips64)
    long __jmpbuf[13];
#elif defined(__arm__)
    long __jmpbuf[16];
#elif defined(__riscv)
    long __jmpbuf[14];
#elif defined(__loongarch64)
    long __jmpbuf[12];
#endif
#elif defined(__CYGWIN__)
#if defined(__amd64) || defined(__amd64__) || defined(__x86_64__) ||           \
    defined(__x86_64)
    long __jmpbuf[8];
#else
#error "Unsupported architecture"
#endif
#else
#error "Unsupported operating system"
#endif
} _st_jmp_buf_t[1];

__asm__(".globl cxy__async_cxt_save");
__asm__(".globl cxy__async_cxt_restore");

#if defined(__APPLE__)
#if defined(__amd64__) || defined(__x86_64__)

#define JB_RBX 0
#define JB_RBP 1
#define JB_R12 2
#define JB_R13 3
#define JB_R14 4
#define JB_R15 5
#define JB_RSP 6
#define JB_PC 7

typedef struct _cxy_jmp_buf {
    long __jump_buf[8];
} cxy__jump_buf_t[1];

int cxy__async_cxt_save(_st_jmp_buf_t env);
__asm__("cxy__async_cxt_save:");
__asm__(".align 16");
/* Save rbx to env[0], *(int64_t*)(rdi+0)=rbx */
__asm__("movq %rbx, (" CXY_STR(JB_RBX) " * 8)(%rdi)");
/* Save rbp to env[1], *(int64_t*)(rdi+1)=rbp */
__asm__("movq %rbp, (" CXY_STR(JB_RBP) " * 8)(%rdi)");
/* Save r12 to env[2], *(int64_t*)(rdi+2)=r12 */
__asm__("movq %r12, (" CXY_STR(JB_R12) " * 8)(%rdi)");
/* Save r13 to env[3], *(int64_t*)(rdi+3)=r13 */
__asm__("movq %r13, (" CXY_STR(JB_R13) " * 8)(%rdi)");
/* Save r14 to env[4], *(int64_t*)(rdi+4)=r14 */
__asm__("movq %r14, (" CXY_STR(JB_R14) " * 8)(%rdi)");
/* Save r15 to env[5], *(int64_t*)(rdi+5)=r15 */
__asm__("movq %r15, (" CXY_STR(JB_R15) " * 8)(%rdi)");
/* Save SP */
__asm__("leaq 8(%rsp), %r8");
/* Save r8(rsp) to env[6], *(int64_t*)(rdi+6)=r8 */
__asm__("movq % r8, (" CXY_STR(JB_RSP) " * 8)(% rdi)");
/* Save PC we are returning to */
__asm__("movq (%rsp), %r9");
/* Save r9(PC) to env[7], *(int64_t*)(rdi+7)=r9 */
__asm__("movq %r9, (" CXY_STR(JB_RSP) " *8)(%rdi)");
/* Reset rax to 0 */
__asm__("xorq %rax, %rax");
__asm__("ret");

void cxy__async_cxt_restore(_st_jmp_buf_t env, int val);
__asm__("cxy__async_cxt_restore:");
__asm__(".align 16");
/* Load rbx from env[0] */
__asm__("movq (" CXY_STR(JB_RBX) " * 8)(%rdi), %rbx");
/* Load rbp from env[1] */
__asm__("movq (" CXY_STR(JB_RBP) " * 8)(%rdi), %rbp");
/* Load r12 from env[2] */
__asm__("movq (" CXY_STR(JB_R12) " * 8)(%rdi), %r12");
/* Load r13 from env[3] */
__asm__("movq (" CXY_STR(JB_R13) " * 8)(%rdi), %r13");
/* Load r14 from env[4] */
__asm__("movq (" CXY_STR(JB_R14) " * 8)(%rdi), %r14");
/* Load r15 from env[5] */
__asm__("movq (" CXY_STR(JB_R15) " * 8)(%rdi), %r15");
/* Set return value, esi is param1 val, the eax is return value */
__asm__("test %esi, %esi");
__asm__("mov $01, %eax");
__asm__("cmove %eax, %esi");
__asm__("mov %esi, %eax");
/* Restore PC and RSP */
/* Load r8(PC) from env[7] */
__asm__("movq (" CXY_STR(JB_PC) "* 8)(%rdi), %r8");
/* Load rsp from env[6] */
__asm__("movq (" CXY_STR(JB_RSP) " * 8)(%rdi), %rsp");
/* Jump to saved PC, Jump to r8(PC) */
__asm__("jmpq *%r8");

#elif defined(__aarch64__)

typedef struct _cxy_jmp_buf {
    long __jump_buf[22];
} cxy__jump_buf_t[1];

#define JB_X19 0
#define JB_X20 1
#define JB_X21 2
#define JB_X22 3
#define JB_X23 4
#define JB_X24 5
#define JB_X25 6
#define JB_X26 7
#define JB_X27 8
#define JB_X28 9
/* r29 and r30 are used as the frame register and link register (avoid) */
#define JB_X29 10
#define JB_LR 11

/* FP registers */
#define JB_D8 14
#define JB_D9 15
#define JB_D10 16
#define JB_D11 17
#define JB_D12 18
#define JB_D13 19
#define JB_D14 20
#define JB_D15 21

int cxy__async_cxt_save(_st_jmp_buf_t env);
__asm__("cxy__async_cxt_save:");
__asm__(".align 4");

__asm__("stp x19, x20, [x0, #" CXY_STR(JB_X19) " << 3]");
__asm__("stp x21, x22, [x0, #" CXY_STR(JB_X21) " << 3]");
__asm__("stp x23, x24, [x0, #" CXY_STR(JB_X23) " << 3]");
__asm__("stp x25, x26, [x0, #" CXY_STR(JB_X25) " << 3]");
__asm__("stp x27, x28, [x0, #" CXY_STR(JB_X27) " << 3]");
__asm__("stp x29, x30, [x0, #" CXY_STR(JB_X29) " << 3]");

__asm__("stp  d8,  d9, [x0, #" CXY_STR(JB_D8) " << 3]");
__asm__("stp d10, d11, [x0, #" CXY_STR(JB_D10) " << 3]");
__asm__("stp d12, d13, [x0, #" CXY_STR(JB_D12) " << 3]");
__asm__("stp d14, d15, [x0, #" CXY_STR(JB_D14) " << 3]");
__asm__("mov x2, sp str x2, [ x0, #" CXY_STR(JB_SP) " << 3]");

__asm__("mov x0, #0");
__asm__("ret");

void cxy__async_cxt_restore(_st_jmp_buf_t env, int val);
__asm__("cxy__async_cxt_restore:");
__asm__(".align 4");
__asm__("ldp x19, x20, [x0, #" CXY_STR(JB_X19) " << 3]");
__asm__("ldp x21, x22, [x0, #" CXY_STR(JB_X21) " << 3]");
__asm__("ldp x23, x24, [x0, #" CXY_STR(JB_X23) " << 3]");
__asm__("ldp x25, x26, [x0, #" CXY_STR(JB_X25) " << 3]");
__asm__("ldp x27, x28, [x0, #" CXY_STR(JB_X27) " << 3]");

__asm__("ldp x29, x30, [x0, #" CXY_STR(JB_X29) " <<3]");

__asm__("ldp  d8,  d9, [x0, #" CXY_STR(JB_D8) " << 3]");
__asm__("ldp d10, d11, [x0, #" CXY_STR(JB_D10) " << 3]");
__asm__("ldp d12, d13, [x0, #" CXY_STR(JB_D12) " << 3]");
__asm__("ldp d14, d15, [x0, #" CXY_STR(JB_D14) " << 3]");

__asm__("ldr x5, [x0, #" CXY_STR(JB_SP) " << 3]");
__asm__("mov sp, x5");

/* x0 = (x1 || 1); */
__asm__("cmp x1, #0");
__asm__("mov x0, #1");
__asm__("csel x0, x1, x0, ne");
__asm__("ret");

#endif
#endif

#define CXY_SET_JMP(env) cxy__async_cxt_save(env)
#define CXY_LONG_JMP(env, val) cxy__async_cxt_restore(env, val)

#if defined(__APPLE__)

#define MD_USE_BSD_ANON_MMAP
#define MD_ACCEPT_NB_INHERITED
#define MD_HAVE_SOCKLEN_T

#if defined(__amd64__) || defined(__x86_64__)
#define CXY_GET_SP(_t) *((long *)&((_t)->context[0].__jmpbuf[6]))
#elif defined(__aarch64__)
#define CXY_GET_SP(_t) *((long *)&((_t)->context[0].__jmpbuf[13]))
#else
#error "Unknown CPU architecture"
#endif

#define CXY_INIT_CONTEXT(_thread, _sp, _main)                                  \
    {                                                                          \
        if (CXY_SETJMP((_thread)->context))                                    \
            _main();                                                           \
        CXY_GET_SP(_thread) = (long)(_sp);                                     \
    }

#define CXY_GET_UTIME()                                                        \
    ({                                                                         \
        struct timeval LINE_VAR(tv);                                           \
        (void)gettimeofday(&LINE_VAR(tv), NULL);                               \
        (LINE_VAR(tv).tv_sec * 1000000LL + LINE_VAR(tv).tv_usec);              \
    })

#endif

#ifndef MD_STACK_PAD_SIZE
#define MD_STACK_PAD_SIZE 128
#endif

/* merge from
 * https://github.com/toffaletti/state-threads/commit/7f57fc9acc05e657bca1223f1e5b9b1a45ed929b
 */
#ifndef MD_VALGRIND
#ifndef NVALGRIND
#define NVALGRIND
#endif
#else
#undef NVALGRIND
#endif
