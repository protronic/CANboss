/**
 * co_node.h
 *
 * Gemeinsame CANopenNode-Stack-Anbindung aller CANboss-Apps
 * (Monitor-TUI, Touch-Panel, Demo-Knoten).
 *
 * Die App uebergibt ihr Objektverzeichnis (CO_MULTIPLE_OD: OD_t* +
 * CO_config_t aus dem generierten <praefix>_INIT_CONFIG-Makro) und das
 * CAN-Backend; die Schicht faehrt RX- und Mainline-Thread und bietet
 * blockierende SDO-Client-Transfers (sofern das OD einen SDO-Client
 * hat), die sich mehrere Aufrufer per Mutex teilen.
 */

#ifndef CB_CO_NODE_H_
#define CB_CO_NODE_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "CANopen.h" /* OD_t, CO_config_t (CO_MULTIPLE_OD), CO_t */

#include "can_if.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Timeout eines einzelnen SDO-Transfers */
#define CB_CO_SDO_TIMEOUT_MS 500u

/* Default-Node-ID des Masters/Panels (canboss_master.eds: Node 127) */
#define CB_CO_NODE_ID_DEFAULT 127u

/* Hook fuer Raw-Frames (z.B. PoC-Hallenlicht): wird im RX-Thread fuer
 * jeden empfangenen klassischen Datenframe aufgerufen. Vor
 * cb_co_start() setzen; NULL = aus. */
typedef void (*cb_co_raw_rx_hook_t)(uint16_t ident, const uint8_t* data, uint8_t dlc);
void cb_co_set_raw_rx_hook(cb_co_raw_rx_hook_t hook);

/* Stack starten: Backend oeffnen, CANopenNode mit dem uebergebenen OD
 * initialisieren, RX- und Mainline-Thread starten.
 * Rueckgabe 0 bei Erfolg, sonst -1 (Fehlertext via cb_co_error()). */
int cb_co_start(OD_t* od, const CO_config_t* config, const cb_can_backend_t* backend, const char* device,
                uint32_t bitrate, uint8_t own_node_id);

/* Stack anhalten, Threads beenden, Backend schliessen. */
void cb_co_stop(void);

/* true, wenn der Stack laeuft (sonst Offline-Modus). */
bool cb_co_connected(void);

/* Letzter Fehlertext von cb_co_start() */
const char* cb_co_error(void);

/* CO_t* des laufenden Stacks (NULL wenn nicht gestartet); fuer direkte
 * OD-Zugriffe (z.B. Berry-Bindings). Zugriffe auf OD-Daten mit
 * CO_LOCK_OD/CO_UNLOCK_OD schuetzen. */
CO_t* cb_co_handle(void);

/* Blockierender SDO-Upload (lesen) vom entfernten Knoten.
 * Rueckgabe 0 bei Erfolg (*read_size gesetzt), sonst -1 und
 * *abort_code enthaelt den SDO-Abortcode (0 = lokaler Fehler/Timeout).
 * Thread-sicher (CB_MUTEX_SDO). */
int cb_co_sdo_read(uint8_t node_id, uint16_t index, uint8_t sub, uint8_t* buf, size_t buf_size, size_t* read_size,
                   uint32_t* abort_code);

/* Blockierender SDO-Download (schreiben) zum entfernten Knoten. */
int cb_co_sdo_write(uint8_t node_id, uint16_t index, uint8_t sub, const uint8_t* data, size_t len,
                    uint32_t* abort_code);

/* Datenquelle fuer cb_co_sdo_write_stream(): bis zu max Bytes nach buf
 * liefern. Rueckgabe: gelesene Bytes, 0 = Ende der Daten, <0 = Fehler
 * (Transfer wird abgebrochen). Wird im Aufruferthread gerufen. */
typedef int (*cb_co_stream_read_t)(void* ctx, uint8_t* buf, size_t max);

/* Fortschrittsmeldung waehrend cb_co_sdo_write_stream() (ca. alle 4 KiB
 * und einmal am Ende); transferred = bestaetigte Bytes. NULL = aus. */
typedef void (*cb_co_stream_progress_t)(void* ctx, size_t transferred, size_t total);

/* Blockierender, streamender SDO-Download grosser Daten (z.B. Firmware
 * an 0x1F50:1): die Daten werden stueckweise per read_cb nachgeladen,
 * der Gesamtumfang total_size muss vorab bekannt sein. Nutzt SDO-
 * Block-Transfer, wenn der Stack damit uebersetzt ist
 * (CO_CONFIG_SDO_CLI_BLOCK, siehe port/CO_driver_target.h), sonst
 * segmentierte Transfers. timeout_ms gilt je Antwort des Servers.
 * Rueckgabe wie cb_co_sdo_write(). Thread-sicher (CB_MUTEX_SDO). */
int cb_co_sdo_write_stream(uint8_t node_id, uint16_t index, uint8_t sub, size_t total_size, uint16_t timeout_ms,
                           cb_co_stream_read_t read_cb, void* read_ctx, cb_co_stream_progress_t progress_cb,
                           void* progress_ctx, uint32_t* abort_code);

/* SDO-Abortcode als Klartext (statischer Puffer). */
const char* cb_co_abort_str(uint32_t abort_code);

#ifdef __cplusplus
}
#endif

#endif /* CB_CO_NODE_H_ */
