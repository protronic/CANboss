/**
 * co_node.h
 *
 * CANopenNode-Stack des CANboss-Monitors (Node 127, OD aus
 * od/canboss_master.h) plus blockierende SDO-Client-Transfers.
 *
 * Ersetzt die GTWA-Serial-Schicht des Rust-Originals
 * (serial_gtwa.rs/gtwa.rs): statt JSON-Kommandos an ein Gateway werden
 * SDO-Upload/-Download direkt ueber den Bus gefahren.
 */

#ifndef CB_CO_NODE_H_
#define CB_CO_NODE_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "can_if.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Node-ID des Monitors selbst (canboss_master.eds: Node 127) */
#define CB_CO_NODE_ID_DEFAULT 127u

/* Timeout eines einzelnen SDO-Transfers */
#define CB_CO_SDO_TIMEOUT_MS 500u

/* Stack starten: Backend oeffnen, CANopenNode initialisieren,
 * RX- und Mainline-Thread starten.
 * Rueckgabe 0 bei Erfolg, sonst -1 (Fehlertext via cb_co_error()). */
int cb_co_start(const cb_can_backend_t* backend, const char* device, uint32_t bitrate, uint8_t own_node_id);

/* Stack anhalten, Threads beenden, Backend schliessen. */
void cb_co_stop(void);

/* true, wenn der Stack laeuft (sonst Offline-Modus wie im Original). */
bool cb_co_connected(void);

/* Letzter Fehlertext von cb_co_start() */
const char* cb_co_error(void);

/* Blockierender SDO-Upload (lesen) vom entfernten Knoten.
 * Rueckgabe 0 bei Erfolg (*read_size gesetzt), sonst -1 und
 * *abort_code enthaelt den SDO-Abortcode (0 = lokaler Fehler). */
int cb_co_sdo_read(uint8_t node_id, uint16_t index, uint8_t sub, uint8_t* buf, size_t buf_size, size_t* read_size,
                   uint32_t* abort_code);

/* Blockierender SDO-Download (schreiben) zum entfernten Knoten. */
int cb_co_sdo_write(uint8_t node_id, uint16_t index, uint8_t sub, const uint8_t* data, size_t len,
                    uint32_t* abort_code);

/* SDO-Abortcode als Klartext (statischer Puffer). */
const char* cb_co_abort_str(uint32_t abort_code);

#ifdef __cplusplus
}
#endif

#endif /* CB_CO_NODE_H_ */
