#ifndef __MINICORO_H__
#define __MINICORO_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Public API qualifier. */
#ifndef MCO_API
#define MCO_API extern
#endif

/* Size of coroutine storage buffer. */
#ifndef MCO_DEFAULT_STORAGE_SIZE
#define MCO_DEFAULT_STORAGE_SIZE 1024
#endif

#include <stddef.h> /* for size_t */

/* ---------------------------------------------------------------------------------------------- */

/* Coroutine states. */
typedef enum mco_state {
    MCO_DEAD = 0,  /* The coroutine has finished normally or was uninitialized before finishing. */
    MCO_NORMAL,    /* The coroutine is active but not running (that is, it has resumed another coroutine). */
    MCO_RUNNING,   /* The coroutine is active and running. */
    MCO_SUSPENDED  /* The coroutine is suspended (in a call to yield, or it has not started running yet). */
} mco_state;

/* Coroutine result codes. */
typedef enum mco_result {
    MCO_SUCCESS = 0,
    MCO_GENERIC_ERROR,
    MCO_INVALID_POINTER,
    MCO_INVALID_COROUTINE,
    MCO_NOT_SUSPENDED,
    MCO_NOT_RUNNING,
    MCO_MAKE_CONTEXT_ERROR,
    MCO_SWITCH_CONTEXT_ERROR,
    MCO_NOT_ENOUGH_SPACE,
    MCO_OUT_OF_MEMORY,
    MCO_INVALID_ARGUMENTS,
    MCO_INVALID_OPERATION,
    MCO_STACK_OVERFLOW,
} mco_result;

/* Coroutine structure. */
typedef struct mco_coro mco_coro;

struct mco_coro {
    void *context;
    mco_state state;

    void (*func)(mco_coro *co);

    mco_coro *prev_co;
    void *user_data;
    void *allocator_data;

    void (*free_cb)(void *ptr, void *allocator_data);

    void *stack_base; /* Stack base address, can be used to scan memory in a garbage collector. */
    size_t stack_size;
    unsigned char *storage;
    size_t bytes_stored;
    size_t storage_size;
    void *asan_prev_stack; /* Used by address sanitizer. */
    void *tsan_prev_fiber; /* Used by thread sanitizer. */
    void *tsan_fiber; /* Used by thread sanitizer. */
    size_t magic_number; /* Used to check stack overflow. */
};

/* Structure used to initialize a coroutine. */
typedef struct mco_desc {
    void (*func)(mco_coro *co); /* Entry point function for the coroutine. */
    void *user_data;            /* Coroutine user data, can be get with `mco_get_user_data`. */
    /* Custom allocation interface. */
    void *(*malloc_cb)(size_t size, void *allocator_data); /* Custom allocation function. */
    void (*free_cb)(void *ptr, void *allocator_data);     /* Custom deallocation function. */
    void *allocator_data;       /* User data pointer passed to `malloc`/`free` allocation functions. */
    size_t storage_size;        /* Coroutine storage size, to be used with the storage APIs. */
    /* These must be initialized only through `mco_init_desc`. */
    size_t coro_size;           /* Coroutine structure size. */
    size_t stack_size;          /* Coroutine stack size. */
} mco_desc;

/* Coroutine functions. */
MCO_API mco_desc mco_desc_init(void (*func)(mco_coro *co),
                               size_t stack_size);  /* Initialize description of a coroutine. When stack size is 0 then MCO_DEFAULT_STACK_SIZE is used. */
MCO_API mco_result mco_init(mco_coro *co, mco_desc *desc);                      /* Initialize the coroutine. */
MCO_API mco_result mco_uninit(
        mco_coro *co);                                    /* Uninitialize the coroutine, may fail if it's not dead or suspended. */
MCO_API mco_result
mco_create(mco_coro **out_co, mco_desc *desc);               /* Allocates and initializes a new coroutine. */
MCO_API mco_result mco_destroy(
        mco_coro *co);                                   /* Uninitialize and deallocate the coroutine, may fail if it's not dead or suspended. */
MCO_API mco_result
mco_resume(mco_coro *co);                                    /* Starts or continues the execution of the coroutine. */
MCO_API mco_result
mco_yield(mco_coro *co);                                     /* Suspends the execution of a coroutine. */
MCO_API mco_state
mco_status(mco_coro *co);                                     /* Returns the status of the coroutine. */
MCO_API void *mco_get_user_data(
        mco_coro *co);                                  /* Get coroutine user data supplied on coroutine creation. */

/* Storage interface functions, used to pass values between yield and resume. */
MCO_API mco_result mco_push(mco_coro *co, const void *src,
                            size_t len); /* Push bytes to the coroutine storage. Use to send values between yield and resume. */
MCO_API mco_result mco_pop(mco_coro *co, void *dest,
                           size_t len);       /* Pop bytes from the coroutine storage. Use to get values between yield and resume. */
MCO_API mco_result
mco_peek(mco_coro *co, void *dest, size_t len);      /* Like `mco_pop` but it does not consumes the storage. */
MCO_API size_t mco_get_bytes_stored(
        mco_coro *co);                      /* Get the available bytes that can be retrieved with a `mco_pop`. */
MCO_API size_t mco_get_storage_size(mco_coro *co);                      /* Get the total storage size. */

/* Misc functions. */
MCO_API mco_coro *mco_running(void);                        /* Returns the running coroutine for the current thread. */
MCO_API const char *mco_result_description(mco_result res); /* Get the description of a result. */

#ifdef __cplusplus
}
#endif

#endif
