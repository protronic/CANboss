/**
 * osal_zephyr.c
 *
 * OS-Abstraktion fuer Zephyr: k_thread mit statischen Stacks, k_mutex,
 * k_uptime_ticks, k_sleep. (Zephyrs POSIX-Schicht ist auf native_sim
 * mit Host-libc nicht verfuegbar, daher direkt die Kernel-API.)
 */

#include "osal.h"

#include <stdbool.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>

#define CB_THREAD_STACK_SIZE 8192
#define CB_THREAD_PRIO       K_PRIO_PREEMPT(2)

static K_THREAD_STACK_ARRAY_DEFINE(cb_stacks, CB_THREAD_COUNT, CB_THREAD_STACK_SIZE);
static struct k_thread cb_threads[CB_THREAD_COUNT];
static k_tid_t cb_tids[CB_THREAD_COUNT];

static struct k_mutex cb_mutexes[CB_MUTEX_COUNT];

static int
cb_osal_init(void) {
    for (int i = 0; i < CB_MUTEX_COUNT; i++) {
        k_mutex_init(&cb_mutexes[i]);
    }
    return 0;
}

SYS_INIT(cb_osal_init, POST_KERNEL, 99);

typedef struct {
    void (*fn)(void*);
    void* arg;
} cb_thread_ctx_t;

static cb_thread_ctx_t cb_thread_ctx[CB_THREAD_COUNT];

static void
cb_thread_trampoline(void* p1, void* p2, void* p3) {
    cb_thread_ctx_t* ctx = (cb_thread_ctx_t*)p1;
    (void)p2;
    (void)p3;
    ctx->fn(ctx->arg);
}

int
cb_thread_start(int slot, void (*fn)(void*), void* arg) {
    if (slot < 0 || slot >= CB_THREAD_COUNT) {
        return -1;
    }
    cb_thread_ctx[slot].fn = fn;
    cb_thread_ctx[slot].arg = arg;
    cb_tids[slot] = k_thread_create(&cb_threads[slot], cb_stacks[slot], CB_THREAD_STACK_SIZE, cb_thread_trampoline,
                                    &cb_thread_ctx[slot], NULL, NULL, CB_THREAD_PRIO, 0, K_NO_WAIT);
    return cb_tids[slot] != NULL ? 0 : -1;
}

void
cb_thread_join(int slot) {
    if (slot >= 0 && slot < CB_THREAD_COUNT && cb_tids[slot] != NULL) {
        k_thread_join(cb_tids[slot], K_FOREVER);
        cb_tids[slot] = NULL;
    }
}

void
cb_mutex_lock(int slot) {
    k_mutex_lock(&cb_mutexes[slot], K_FOREVER);
}

void
cb_mutex_unlock(int slot) {
    k_mutex_unlock(&cb_mutexes[slot]);
}

uint64_t
cb_now_us(void) {
    return (uint64_t)k_ticks_to_us_floor64(k_uptime_ticks());
}

void
cb_sleep_us(uint32_t us) {
    k_sleep(K_USEC(us));
}
