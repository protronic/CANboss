/**
 * osal.h
 *
 * Minimale OS-Abstraktion der Zephyr-App: Arbeitsthreads (CAN-RX,
 * CANopen-Mainline, SDO-Worker), Mutexe, monotone Zeit und Schlafen.
 * Implementierung: osal_zephyr.c (k_thread mit statischen Stacks,
 * k_mutex, k_uptime, k_sleep). Gleiche Schnittstelle wie im
 * CANboss-Repo, damit die CANopen-Schicht identisch bleibt.
 */

#ifndef CB_OSAL_H_
#define CB_OSAL_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Feste Thread-Slots (statische Stacks) */
#define CB_THREAD_CAN_RX     0
#define CB_THREAD_MAINLINE   1
#define CB_THREAD_SDO_WORKER 2
#define CB_THREAD_COUNT      3

/* Feste Mutex-Slots */
#define CB_MUTEX_CAN_SEND 0
#define CB_MUTEX_EMCY     1
#define CB_MUTEX_OD       2
#define CB_MUTEX_SDO      3
#define CB_MUTEX_BERRY    4
#define CB_MUTEX_COUNT    5

int cb_thread_start(int slot, void (*fn)(void*), void* arg);
void cb_thread_join(int slot);

void cb_mutex_lock(int slot);
void cb_mutex_unlock(int slot);

uint64_t cb_now_us(void);
void cb_sleep_us(uint32_t us);

#ifdef __cplusplus
}
#endif

#endif /* CB_OSAL_H_ */
