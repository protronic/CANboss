/**
 * osal_posix.c
 *
 * OS-Abstraktion fuer Linux/POSIX: pthreads, clock_gettime, usleep.
 */

#include "osal.h"

#include <pthread.h>
#include <time.h>
#include <unistd.h>

static pthread_t cb_threads[CB_THREAD_COUNT];

static pthread_mutex_t cb_mutexes[CB_MUTEX_COUNT] = {
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
};

typedef struct {
    void (*fn)(void*);
    void* arg;
} cb_thread_ctx_t;

static cb_thread_ctx_t cb_thread_ctx[CB_THREAD_COUNT];

static void*
cb_thread_trampoline(void* arg) {
    cb_thread_ctx_t* ctx = (cb_thread_ctx_t*)arg;
    ctx->fn(ctx->arg);
    return NULL;
}

int
cb_thread_start(int slot, void (*fn)(void*), void* arg) {
    if (slot < 0 || slot >= CB_THREAD_COUNT) {
        return -1;
    }
    cb_thread_ctx[slot].fn = fn;
    cb_thread_ctx[slot].arg = arg;
    return pthread_create(&cb_threads[slot], NULL, cb_thread_trampoline, &cb_thread_ctx[slot]) == 0 ? 0 : -1;
}

void
cb_thread_join(int slot) {
    if (slot >= 0 && slot < CB_THREAD_COUNT) {
        pthread_join(cb_threads[slot], NULL);
    }
}

void
cb_mutex_lock(int slot) {
    pthread_mutex_lock(&cb_mutexes[slot]);
}

void
cb_mutex_unlock(int slot) {
    pthread_mutex_unlock(&cb_mutexes[slot]);
}

uint64_t
cb_now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

void
cb_sleep_us(uint32_t us) {
    usleep(us);
}
