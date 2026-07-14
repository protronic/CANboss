/**
 * osal.h
 *
 * Minimale OS-Abstraktion des CANboss-Monitors: zwei Arbeitsthreads
 * (CAN-RX, CANopen-Mainline), Mutexe, monotone Zeit und Schlafen.
 *
 *   - osal_posix.c   Linux/POSIX: pthreads, clock_gettime, usleep
 *   - osal_zephyr.c  Zephyr: k_thread (statische Stacks), k_mutex,
 *                    k_uptime, k_sleep
 */

#ifndef CB_OSAL_H_
#define CB_OSAL_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Feste Thread-Slots (statische Stacks im Zephyr-Build) */
#define CB_THREAD_CAN_RX   0
#define CB_THREAD_MAINLINE 1
#define CB_THREAD_COUNT    2

/* Feste Mutex-Slots */
#define CB_MUTEX_CAN_SEND 0
#define CB_MUTEX_EMCY     1
#define CB_MUTEX_OD       2
#define CB_MUTEX_SDO      3
#define CB_MUTEX_COUNT    4

/* Thread im Slot starten. Rueckgabe 0 bei Erfolg. */
int cb_thread_start(int slot, void (*fn)(void*), void* arg);

/* Auf Thread-Ende warten (Funktion fn ist zurueckgekehrt). */
void cb_thread_join(int slot);

void cb_mutex_lock(int slot);
void cb_mutex_unlock(int slot);

/* Monotone Zeit in Mikrosekunden */
uint64_t cb_now_us(void);

void cb_sleep_us(uint32_t us);

#ifdef __cplusplus
}
#endif

#endif /* CB_OSAL_H_ */
