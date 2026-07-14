/**
 * co_zephyr.h
 *
 * CANopenNode-Stack der CANbossTouch-Zephyr-App (Node 127, OD aus
 * App/OD) - Pendant zu CO_app_STM32 des STM32-Builds.
 *
 * Bietet zusaetzlich blockierende SDO-Client-Transfers, die sich der
 * canboss_sdo-Worker (LVGL-UI) und die Berry-Bindings teilen
 * (untereinander per Mutex serialisiert).
 */

#ifndef CB_CO_ZEPHYR_H_
#define CB_CO_ZEPHYR_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Timeout eines einzelnen SDO-Transfers */
#define CB_CO_SDO_TIMEOUT_MS 500u

/* Stack starten (Backend: Zephyr-CAN, chosen zephyr,canbus).
 * Rueckgabe 0 bei Erfolg, sonst -1 (Fehlertext via cb_co_error()). */
int cb_co_start(uint8_t own_node_id);

void cb_co_stop(void);

bool cb_co_connected(void);

const char* cb_co_error(void);

/* CO_t* des laufenden Stacks (NULL wenn nicht gestartet); fuer
 * OD-Zugriffe der Berry-Bindings. Zugriffe auf OD-Daten mit
 * CO_LOCK_OD/CO_UNLOCK_OD schuetzen. */
void* cb_co_handle(void);

/* Blockierender SDO-Upload (lesen) / -Download (schreiben).
 * Rueckgabe 0 bei Erfolg, sonst -1; *abort_code enthaelt den
 * SDO-Abortcode (0 = lokaler Fehler/Timeout). Thread-sicher. */
int cb_co_sdo_read(uint8_t node_id, uint16_t index, uint8_t sub, uint8_t* buf, size_t buf_size, size_t* read_size,
                   uint32_t* abort_code);
int cb_co_sdo_write(uint8_t node_id, uint16_t index, uint8_t sub, const uint8_t* data, size_t len,
                    uint32_t* abort_code);

const char* cb_co_abort_str(uint32_t abort_code);

#ifdef __cplusplus
}
#endif

#endif /* CB_CO_ZEPHYR_H_ */
