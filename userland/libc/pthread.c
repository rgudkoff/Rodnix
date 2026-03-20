/**
 * @file pthread.c
 * @brief POSIX threads — RodNIX userland implementation
 *
 * Thread model:
 *   pthread_create() allocates a stack, builds a TLS block (pthread_t lives
 *   at the base of the TLS block, FS points to it), then calls clone() with
 *   CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD |
 *   CLONE_SETTLS | CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID.
 *
 *   The new thread starts at __pthread_entry, which calls start_routine(arg)
 *   and then pthread_exit().
 *
 * Synchronisation:
 *   Mutex uses futex (FUTEX_WAIT / FUTEX_WAKE) on a 32-bit lock word.
 *   Condvar uses a sequence-number based futex approach.
 */

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <posix_syscall.h>
#include <sys/mman.h>

/*
 * Assembly trampoline declared in pthread_clone.S.
 * Makes the clone() syscall; in the child jumps to start_fn(start_arg).
 * Returns child TID in parent, never returns in child.
 */
extern long __pthread_do_clone(unsigned long flags,
                               void*         child_stack,
                               int*          ptid,
                               int*          ctid,
                               unsigned long newtls,
                               void        (*start_fn)(void*),
                               void*         start_arg);

/* ------------------------------------------------------------------ */
/* Internal TLS / thread descriptor layout                             */
/* ------------------------------------------------------------------ */

/*
 * __pthread_self_t lives at the start of each thread's TLS block.
 * The FS register points to this struct (self-pointer at FS:0 convention).
 */
typedef struct __pthread_self {
    struct __pthread_self* self;     /* FS:0 — pointer to self (glibc/musl compat) */
    pthread_t              tid;      /* kernel thread id (returned by clone) */
    void*                  retval;   /* pthread_exit() return value */
    uint32_t               detached; /* 1 if detached */
    volatile int32_t       tid_futex;/* kernel writes 0 here on exit (CLONE_CHILD_CLEARTID) */
    /* padding to 64 bytes */
    uint8_t                _pad[24];
} __pthread_self_t;

#define PTHREAD_TLS_SIZE  4096   /* one page per thread TLS block */
#define PTHREAD_STACK_MIN 65536  /* 64 KiB minimum */

/* Default stack size: 2 MiB */
#define PTHREAD_DEFAULT_STACK_SIZE (2 * 1024 * 1024)

/* ------------------------------------------------------------------ */
/* Clone flags                                                         */
/* ------------------------------------------------------------------ */

#define CLONE_VM             0x00000100u
#define CLONE_FS             0x00000200u
#define CLONE_FILES          0x00000400u
#define CLONE_SIGHAND        0x00000800u
#define CLONE_THREAD         0x00010000u
#define CLONE_SETTLS         0x00080000u
#define CLONE_PARENT_SETTID  0x00100000u
#define CLONE_CHILD_CLEARTID 0x00200000u
#define CLONE_CHILD_SETTID   0x01000000u

/* ------------------------------------------------------------------ */
/* Futex operations                                                    */
/* ------------------------------------------------------------------ */

#define FUTEX_WAIT  0
#define FUTEX_WAKE  1

static inline void futex_wait(volatile int32_t* addr, int32_t expected)
{
    posix_futex((int*)addr, FUTEX_WAIT, expected, NULL, NULL, 0);
}

static inline void futex_wake(volatile int32_t* addr, int n)
{
    posix_futex((int*)addr, FUTEX_WAKE, n, NULL, NULL, 0);
}

/* ------------------------------------------------------------------ */
/* Atomic helpers (GCC built-ins)                                      */
/* ------------------------------------------------------------------ */

#define atomic_cas(ptr, old, new) \
    __sync_val_compare_and_swap((ptr), (old), (new))
#define atomic_add(ptr, val) \
    __sync_fetch_and_add((ptr), (val))

/* ------------------------------------------------------------------ */
/* pthread_self — reads FS:0                                           */
/* ------------------------------------------------------------------ */

pthread_t pthread_self(void)
{
    __pthread_self_t* s;
    __asm__ volatile ("mov %%fs:0, %0" : "=r"(s));
    if (!s) {
        return 0;
    }
    return s->tid;
}

int pthread_equal(pthread_t t1, pthread_t t2)
{
    return t1 == t2;
}

/* ------------------------------------------------------------------ */
/* Thread entry trampoline                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    void* (*start_routine)(void*);
    void*  arg;
} __pthread_start_args_t;

__attribute__((noreturn))
static void __pthread_entry(void* arg_raw)
{
    __pthread_start_args_t* sa = (__pthread_start_args_t*)arg_raw;
    void* (*fn)(void*) = sa->start_routine;
    void* fn_arg       = sa->arg;
    /* sa is on the thread's stack — valid until we return */

    void* retval = fn(fn_arg);
    pthread_exit(retval);
}

/* ------------------------------------------------------------------ */
/* pthread_attr                                                        */
/* ------------------------------------------------------------------ */

int pthread_attr_init(pthread_attr_t* attr)
{
    if (!attr) { return EINVAL; }
    memset(attr, 0, sizeof(*attr));
    attr->stacksize = PTHREAD_DEFAULT_STACK_SIZE;
    return 0;
}

int pthread_attr_destroy(pthread_attr_t* attr)
{
    (void)attr;
    return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t* attr, int detachstate)
{
    if (!attr) { return EINVAL; }
    attr->detachstate = (uint32_t)detachstate;
    return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t* attr, int* detachstate)
{
    if (!attr || !detachstate) { return EINVAL; }
    *detachstate = (int)attr->detachstate;
    return 0;
}

int pthread_attr_setstacksize(pthread_attr_t* attr, size_t stacksize)
{
    if (!attr || stacksize < PTHREAD_STACK_MIN) { return EINVAL; }
    attr->stacksize = stacksize;
    return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t* attr, size_t* stacksize)
{
    if (!attr || !stacksize) { return EINVAL; }
    *stacksize = attr->stacksize;
    return 0;
}

/* ------------------------------------------------------------------ */
/* pthread_create                                                      */
/* ------------------------------------------------------------------ */

int pthread_create(pthread_t* thread,
                   const pthread_attr_t* attr,
                   void* (*start_routine)(void*),
                   void* arg)
{
    if (!thread || !start_routine) {
        return EINVAL;
    }

    size_t stack_size = attr ? attr->stacksize : PTHREAD_DEFAULT_STACK_SIZE;
    if (stack_size < PTHREAD_STACK_MIN) {
        stack_size = PTHREAD_STACK_MIN;
    }
    /* Round up to page boundary */
    stack_size = (stack_size + 4095u) & ~(size_t)4095u;

    /*
     * Allocate stack + TLS block in one mmap call.
     * Layout (low → high):
     *   [guard page (4 KiB)] [stack (stack_size)] [TLS block (PTHREAD_TLS_SIZE)]
     * The TLS block sits at the high end; FS points to it.
     * The guard page at the bottom catches stack overflow.
     */
    size_t total = 4096 + stack_size + PTHREAD_TLS_SIZE;
    void* mem = (void*)(long)posix_mmap(NULL, total,
                                        PROT_READ | PROT_WRITE,
                                        MAP_PRIVATE | MAP_ANONYMOUS,
                                        -1, 0);
    if ((long)mem < 0) {
        return ENOMEM;
    }

    /* Guard page: remove write permission (catches stack overflow). */
    posix_mprotect(mem, 4096, PROT_NONE);

    /* TLS block starts at (mem + 4096 + stack_size). */
    uint8_t* tls_block = (uint8_t*)mem + 4096 + stack_size;
    memset(tls_block, 0, PTHREAD_TLS_SIZE);

    __pthread_self_t* tself = (__pthread_self_t*)tls_block;
    tself->self     = tself;
    tself->retval   = NULL;
    tself->detached = attr ? attr->detachstate : 0;
    tself->tid_futex = 1;  /* non-zero; kernel will write 0 on thread exit */

    /*
     * Stack grows downward. We place start args at the very top of the
     * stack area (just below the TLS block) and pass a pointer to __pthread_entry.
     */
    uint8_t* stack_top = (uint8_t*)mem + 4096 + stack_size;
    /* Push __pthread_start_args_t onto the stack */
    stack_top -= sizeof(__pthread_start_args_t);
    __pthread_start_args_t* sa = (__pthread_start_args_t*)stack_top;
    sa->start_routine = start_routine;
    sa->arg           = arg;

    /*
     * 16-byte-align the stack pointer (System V AMD64 ABI).
     * __pthread_do_clone will push start_fn and start_arg onto the child
     * stack before making the syscall, so leave room for two 8-byte words.
     */
    stack_top = (uint8_t*)((uintptr_t)stack_top & ~(uintptr_t)15u);

    *thread = 0; /* filled in below */

    /*
     * __pthread_do_clone (pthread_clone.S) makes the clone() syscall.
     * In the child it pops start_arg into rdi and jumps to __pthread_entry.
     * In the parent it returns the new thread's tid.
     */
    unsigned long clone_flags =
        CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD |
        CLONE_SETTLS | CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID;

    long tid = __pthread_do_clone(clone_flags,
                                  (void*)stack_top,
                                  (int*)&tself->tid_futex,
                                  (int*)&tself->tid_futex,
                                  (unsigned long)(uintptr_t)tself,
                                  __pthread_entry,
                                  sa);
    if (tid < 0) {
        posix_munmap(mem, total);
        return EAGAIN;
    }

    tself->tid = (pthread_t)tid;
    /* Return the tself pointer as the opaque handle so pthread_join can
     * access tid_futex directly without a thread table. */
    *thread = (pthread_t)(uintptr_t)tself;
    return 0;
}

/* ------------------------------------------------------------------ */
/* pthread_join                                                        */
/* ------------------------------------------------------------------ */

int pthread_join(pthread_t thread, void** retval)
{
    if (!thread) {
        return EINVAL;
    }
    /*
     * pthread_t is the tself pointer (set by pthread_create).
     * CLONE_CHILD_CLEARTID makes the kernel zero tid_futex and wake the futex
     * when the thread exits — spin-wait on that word.
     */
    __pthread_self_t* tself = (__pthread_self_t*)(uintptr_t)thread;
    volatile int32_t* futex_word = &tself->tid_futex;
    int32_t v;
    while ((v = *futex_word) != 0) {
        futex_wait(futex_word, v);
    }
    if (retval) {
        *retval = tself->retval;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* pthread_detach / pthread_exit                                       */
/* ------------------------------------------------------------------ */

int pthread_detach(pthread_t thread)
{
    (void)thread;
    return 0; /* TODO */
}

void pthread_exit(void* retval)
{
    /* Store return value in TLS if available */
    __pthread_self_t* s;
    __asm__ volatile ("mov %%fs:0, %0" : "=r"(s));
    if (s) {
        s->retval = retval;
    }
    /* Exit just this thread (not the whole process). */
    posix_thread_exit(0);
    __builtin_unreachable();
}

/* ------------------------------------------------------------------ */
/* Mutex                                                               */
/* ------------------------------------------------------------------ */

int pthread_mutexattr_init(pthread_mutexattr_t* attr)
{
    if (!attr) { return EINVAL; }
    attr->dummy = 0;
    return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t* attr)
{
    (void)attr;
    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t* attr, int type)
{
    if (!attr) { return EINVAL; }
    (void)type;
    return 0;
}

int pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr)
{
    if (!mutex) { return EINVAL; }
    mutex->lock = 0;
    mutex->type = attr ? 0 : 0;
    (void)attr;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t* mutex)
{
    (void)mutex;
    return 0;
}

/*
 * Mutex lock/unlock using a three-state word:
 *   0 = unlocked
 *   1 = locked, no waiters
 *   2 = locked, waiters present
 */
int pthread_mutex_lock(pthread_mutex_t* mutex)
{
    if (!mutex) { return EINVAL; }

    int32_t c = atomic_cas(&mutex->lock, 0, 1);
    if (c != 0) {
        /* Already locked — mark as having waiters and sleep. */
        if (c != 2) {
            c = atomic_cas(&mutex->lock, 1, 2);
        }
        while (c != 0) {
            futex_wait(&mutex->lock, 2);
            c = atomic_cas(&mutex->lock, 0, 2);
        }
    }
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t* mutex)
{
    if (!mutex) { return EINVAL; }
    int32_t c = atomic_cas(&mutex->lock, 0, 1);
    return (c == 0) ? 0 : EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t* mutex)
{
    if (!mutex) { return EINVAL; }
    int32_t prev = __sync_fetch_and_sub(&mutex->lock, 1);
    if (prev != 1) {
        /* There were waiters (lock was 2). */
        mutex->lock = 0;
        futex_wake(&mutex->lock, 1);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Condition variable                                                  */
/* ------------------------------------------------------------------ */

int pthread_condattr_init(pthread_condattr_t* attr)
{
    if (!attr) { return EINVAL; }
    attr->dummy = 0;
    return 0;
}

int pthread_condattr_destroy(pthread_condattr_t* attr)
{
    (void)attr;
    return 0;
}

int pthread_cond_init(pthread_cond_t* cond, const pthread_condattr_t* attr)
{
    if (!cond) { return EINVAL; }
    (void)attr;
    cond->seq     = 0;
    cond->waiters = 0;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t* cond)
{
    (void)cond;
    return 0;
}

int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex)
{
    if (!cond || !mutex) { return EINVAL; }

    atomic_add(&cond->waiters, 1);
    uint32_t seq = cond->seq;

    pthread_mutex_unlock(mutex);
    futex_wait((volatile int32_t*)&cond->seq, (int32_t)seq);
    atomic_add(&cond->waiters, -1);
    pthread_mutex_lock(mutex);
    return 0;
}

int pthread_cond_signal(pthread_cond_t* cond)
{
    if (!cond) { return EINVAL; }
    if (cond->waiters > 0) {
        __sync_fetch_and_add(&cond->seq, 1);
        futex_wake((volatile int32_t*)&cond->seq, 1);
    }
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t* cond)
{
    if (!cond) { return EINVAL; }
    if (cond->waiters > 0) {
        __sync_fetch_and_add(&cond->seq, 1);
        futex_wake((volatile int32_t*)&cond->seq, cond->waiters);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* pthread_once                                                        */
/* ------------------------------------------------------------------ */

int pthread_once(pthread_once_t* once_control, void (*init_routine)(void))
{
    if (!once_control || !init_routine) { return EINVAL; }

    if (once_control->done) {
        return 0;
    }
    if (pthread_mutex_lock(&once_control->mutex) != 0) {
        return EINVAL;
    }
    if (!once_control->done) {
        init_routine();
        once_control->done = 1;
    }
    pthread_mutex_unlock(&once_control->mutex);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Thread-local storage (minimal — 32 keys max)                       */
/* ------------------------------------------------------------------ */

#define PTHREAD_KEYS_MAX 32

static volatile int   g_key_used[PTHREAD_KEYS_MAX];
static void         (*g_key_dtor[PTHREAD_KEYS_MAX])(void*);

/* Per-thread key values stored in TLS block after __pthread_self_t */
static __thread void* g_tls_values[PTHREAD_KEYS_MAX];

int pthread_key_create(pthread_key_t* key, void (*destructor)(void*))
{
    if (!key) { return EINVAL; }
    for (int i = 0; i < PTHREAD_KEYS_MAX; i++) {
        if (atomic_cas(&g_key_used[i], 0, 1) == 0) {
            g_key_dtor[i] = destructor;
            *key = (pthread_key_t)i;
            return 0;
        }
    }
    return EAGAIN;
}

int pthread_key_delete(pthread_key_t key)
{
    if (key >= PTHREAD_KEYS_MAX) { return EINVAL; }
    g_key_used[key] = 0;
    g_key_dtor[key] = NULL;
    return 0;
}

void* pthread_getspecific(pthread_key_t key)
{
    if (key >= PTHREAD_KEYS_MAX) { return NULL; }
    return g_tls_values[key];
}

int pthread_setspecific(pthread_key_t key, const void* value)
{
    if (key >= PTHREAD_KEYS_MAX) { return EINVAL; }
    g_tls_values[key] = (void*)value;
    return 0;
}
