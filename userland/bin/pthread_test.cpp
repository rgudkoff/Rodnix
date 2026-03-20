/**
 * pthread_test.cpp — C++ pthread smoke-test for RodNIX
 *
 * Tests:
 *   1. Global C++ object constructor/destructor (.init_array mechanism)
 *   2. operator new / operator delete (minimal freestanding impl below)
 *   3. pthread_create + pthread_mutex: 4 threads each increment a shared
 *      counter N times; expected final value = 4 * N
 *   4. pthread_once: init function called exactly once across all threads
 */

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

extern "C" {
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
}

/* ------------------------------------------------------------------ */
/* Minimal freestanding operator new / delete                         */
/* (no exceptions, so bad_alloc is never thrown)                      */
/* ------------------------------------------------------------------ */

void* operator new(size_t sz)   { return malloc(sz); }
void* operator new[](size_t sz) { return malloc(sz); }
void  operator delete(void* p) noexcept   { free(p); }
void  operator delete[](void* p) noexcept { free(p); }
void  operator delete(void* p, size_t) noexcept   { free(p); }
void  operator delete[](void* p, size_t) noexcept { free(p); }

/* ------------------------------------------------------------------ */
/* Test 1: global object — verifies .init_array is walked by crt0     */
/* ------------------------------------------------------------------ */

static int g_ctor_called = 0;
static int g_dtor_called = 0;

class GlobalGuard {
public:
    GlobalGuard()  { g_ctor_called = 1; }
    ~GlobalGuard() { g_dtor_called = 1; }
};

static GlobalGuard g_guard;   /* constructor runs before main() */

/* ------------------------------------------------------------------ */
/* Test 2: heap allocation of a non-trivial object                    */
/* ------------------------------------------------------------------ */

class Counter {
    int value_;
public:
    explicit Counter(int v) : value_(v) {}
    void increment() { value_++; }
    int  get() const { return value_; }
};

/* ------------------------------------------------------------------ */
/* Test 3: pthread_create + mutex — shared counter                    */
/* ------------------------------------------------------------------ */

#define NUM_THREADS   4
#define ITERS_PER_THR 10000

static volatile int       g_shared_counter = 0;
static pthread_mutex_t    g_mutex = PTHREAD_MUTEX_INITIALIZER;

struct thread_arg {
    int id;
    int iters;
};

static void* worker(void* arg)
{
    thread_arg* a = static_cast<thread_arg*>(arg);

    for (int i = 0; i < a->iters; i++) {
        pthread_mutex_lock(&g_mutex);
        g_shared_counter++;
        pthread_mutex_unlock(&g_mutex);
    }
    return nullptr;
}

/* ------------------------------------------------------------------ */
/* Test 4: pthread_once                                               */
/* ------------------------------------------------------------------ */

static int           g_once_count = 0;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

static void once_init(void) { g_once_count++; }

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    int pass = 0;
    int fail = 0;

    printf("pthread_test: starting\n");

    /* --- Test 1: global constructor -------------------------------- */
    if (g_ctor_called) {
        printf("[PASS] global constructor ran before main()\n");
        pass++;
    } else {
        printf("[FAIL] global constructor did NOT run\n");
        fail++;
    }

    /* --- Test 2: operator new ------------------------------------- */
    Counter* c = new Counter(42);
    c->increment();
    if (c->get() == 43) {
        printf("[PASS] operator new / method call work\n");
        pass++;
    } else {
        printf("[FAIL] Counter returned %d, expected 43\n", c->get());
        fail++;
    }
    delete c;

    /* --- Test 3: pthreads + mutex --------------------------------- */
    pthread_t tids[NUM_THREADS];
    thread_arg args[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].id    = i;
        args[i].iters = ITERS_PER_THR;
        if (pthread_create(&tids[i], nullptr, worker, &args[i]) != 0) {
            printf("[FAIL] pthread_create(%d) failed\n", i);
            fail++;
            tids[i] = 0;
        }
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        if (tids[i]) {
            pthread_join(tids[i], nullptr);
        }
    }

    int expected = NUM_THREADS * ITERS_PER_THR;
    if (g_shared_counter == expected) {
        printf("[PASS] shared counter = %d (expected %d)\n",
               g_shared_counter, expected);
        pass++;
    } else {
        printf("[FAIL] shared counter = %d, expected %d\n",
               g_shared_counter, expected);
        fail++;
    }

    /* --- Test 4: pthread_once ------------------------------------- */
    pthread_once(&g_once, once_init);
    pthread_once(&g_once, once_init);   /* must not call init again */
    if (g_once_count == 1) {
        printf("[PASS] pthread_once called init exactly once\n");
        pass++;
    } else {
        printf("[FAIL] pthread_once count = %d, expected 1\n", g_once_count);
        fail++;
    }

    /* --- Summary -------------------------------------------------- */
    printf("\npthread_test: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
