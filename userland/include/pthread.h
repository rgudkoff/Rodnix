/**
 * @file pthread.h
 * @brief POSIX threads — RodNIX userland implementation
 *
 * Supports: pthread_create/join/exit, mutex (normal), condvar.
 * Thread creation uses the native clone() syscall (POSIX_SYS_CLONE = 78).
 * Thread-local storage is set via CLONE_SETTLS (FS.Base MSR).
 */

#ifndef _RODNIX_PTHREAD_H
#define _RODNIX_PTHREAD_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Types                                                               */
/* ------------------------------------------------------------------ */

typedef uint64_t pthread_t;       /* thread handle = kernel thread_id */
typedef uint32_t pthread_key_t;

typedef struct {
    uint32_t detachstate;
    uint32_t schedpolicy;
    int32_t  schedpriority;
    size_t   stacksize;
    void*    stackaddr;
} pthread_attr_t;

typedef struct {
    volatile int32_t lock;  /* 0 = unlocked, 1 = locked, 2 = locked+waiters */
    int32_t          type;
} pthread_mutex_t;

typedef struct {
    int32_t dummy;
} pthread_mutexattr_t;

typedef struct {
    volatile uint32_t seq;     /* sequence number, odd = broadcast pending */
    volatile int32_t  waiters; /* count of waiting threads */
} pthread_cond_t;

typedef struct {
    int32_t dummy;
} pthread_condattr_t;

typedef struct {
    volatile uint32_t done;
    pthread_mutex_t   mutex;
} pthread_once_t;

/* ------------------------------------------------------------------ */
/* Detach state                                                        */
/* ------------------------------------------------------------------ */

#define PTHREAD_CREATE_JOINABLE  0
#define PTHREAD_CREATE_DETACHED  1

/* ------------------------------------------------------------------ */
/* Mutex types / initialisers                                          */
/* ------------------------------------------------------------------ */

#define PTHREAD_MUTEX_NORMAL     0
#define PTHREAD_MUTEX_RECURSIVE  1
#define PTHREAD_MUTEX_ERRORCHECK 2

#define PTHREAD_MUTEX_INITIALIZER    { 0, PTHREAD_MUTEX_NORMAL }
#define PTHREAD_COND_INITIALIZER     { 0, 0 }
#define PTHREAD_ONCE_INIT            { 0, PTHREAD_MUTEX_INITIALIZER }

/* ------------------------------------------------------------------ */
/* pthread_attr                                                        */
/* ------------------------------------------------------------------ */

int pthread_attr_init(pthread_attr_t* attr);
int pthread_attr_destroy(pthread_attr_t* attr);
int pthread_attr_setdetachstate(pthread_attr_t* attr, int detachstate);
int pthread_attr_getdetachstate(const pthread_attr_t* attr, int* detachstate);
int pthread_attr_setstacksize(pthread_attr_t* attr, size_t stacksize);
int pthread_attr_getstacksize(const pthread_attr_t* attr, size_t* stacksize);

/* ------------------------------------------------------------------ */
/* Thread lifecycle                                                    */
/* ------------------------------------------------------------------ */

int   pthread_create(pthread_t* thread,
                     const pthread_attr_t* attr,
                     void* (*start_routine)(void*),
                     void* arg);
int   pthread_join(pthread_t thread, void** retval);
int   pthread_detach(pthread_t thread);
void  pthread_exit(void* retval) __attribute__((noreturn));
pthread_t pthread_self(void);
int   pthread_equal(pthread_t t1, pthread_t t2);

/* ------------------------------------------------------------------ */
/* Mutex                                                               */
/* ------------------------------------------------------------------ */

int pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr);
int pthread_mutex_destroy(pthread_mutex_t* mutex);
int pthread_mutex_lock(pthread_mutex_t* mutex);
int pthread_mutex_trylock(pthread_mutex_t* mutex);
int pthread_mutex_unlock(pthread_mutex_t* mutex);

int pthread_mutexattr_init(pthread_mutexattr_t* attr);
int pthread_mutexattr_destroy(pthread_mutexattr_t* attr);
int pthread_mutexattr_settype(pthread_mutexattr_t* attr, int type);

/* ------------------------------------------------------------------ */
/* Condition variable                                                  */
/* ------------------------------------------------------------------ */

int pthread_cond_init(pthread_cond_t* cond, const pthread_condattr_t* attr);
int pthread_cond_destroy(pthread_cond_t* cond);
int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex);
int pthread_cond_signal(pthread_cond_t* cond);
int pthread_cond_broadcast(pthread_cond_t* cond);

int pthread_condattr_init(pthread_condattr_t* attr);
int pthread_condattr_destroy(pthread_condattr_t* attr);

/* ------------------------------------------------------------------ */
/* Once                                                                */
/* ------------------------------------------------------------------ */

int pthread_once(pthread_once_t* once_control, void (*init_routine)(void));

/* ------------------------------------------------------------------ */
/* Thread-local storage (minimal stub)                                 */
/* ------------------------------------------------------------------ */

int pthread_key_create(pthread_key_t* key, void (*destructor)(void*));
int pthread_key_delete(pthread_key_t key);
void* pthread_getspecific(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void* value);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _RODNIX_PTHREAD_H */
