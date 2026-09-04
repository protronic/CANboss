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

/* Hook fuer Raw-Frames (z.B. PoC-Hallenlicht, PDO-Monitor, SLCAN):
 * wird im RX-Thread fuer jeden empfangenen klassischen 11-Bit-Frame
 * aufgerufen - auch fuer RTR-Frames (rtr=true, data dann ohne
 * Bedeutung), die der CANopen-Stack selbst nicht sieht. Vor
 * cb_co_start() setzen; NULL = aus. */
typedef void (*cb_co_raw_rx_hook_t)(uint16_t ident, const uint8_t* data, uint8_t dlc, bool rtr);
void cb_co_set_raw_rx_hook(cb_co_raw_rx_hook_t hook);

/* Einen klassischen 11-Bit-Frame direkt ueber das Backend senden -
 * parallel zum CANopen-Verkehr (SLCAN-Bruecke des Gateways). Das
 * Backend (Zephyr can_send bzw. SocketCAN write) ist thread-sicher.
 * Rueckgabe 0 bei Erfolg, -ENODEV wenn der Stack nicht laeuft,
 * -EIO bei Sendefehler. */
int cb_co_raw_send(uint16_t ident, const uint8_t* data, uint8_t dlc, bool rtr);

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

/* Ein per Heartbeat-Consumer aktiver Knoten (mindestens ein HB
 * empfangen, HBstate == ACTIVE) und sein letzter NMT-State. */
typedef struct {
    uint8_t node_id;
    int8_t nmt_state; /* 0 init, 4 stop, 5 op, 127 pre, -1 unbekannt */
} cb_co_nodstat_ent_t;

/* Snapshot der vom Heartbeat-Consumer gesehenen Knoten.
 * filter_id 0 = alle aktiven, 1..127 = nur diese Node-ID.
 * Schreibt bis zu max Eintraege nach out. Rueckgabe: Anzahl (0..n),
 * -ENODEV wenn der Stack nicht laeuft, -ENOTSUP wenn kein
 * Heartbeat-Consumer konfiguriert ist (kein 0x1016-Eintrag). */
int cb_co_nodstat(uint8_t filter_id, cb_co_nodstat_ent_t* out, size_t max);

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

/* --- Gateway-ASCII (CiA 309-3) ------------------------------------
 *
 * Nur nutzbar, wenn der Stack mit CO_CONFIG_GTW_ASCII uebersetzt ist
 * (CO_DRIVER_CUSTOM der App, siehe apps/gateway) UND die App im
 * CO_config_t CNT_GTWA=1 setzt. Kommandotext (z.B. "[1] 1 r 0x2500 2 u32\n")
 * wird eingespeist, die Antwortzeilen ("[1] 123\r\n") liefert der
 * Read-Callback aus dem Mainline-Thread.
 *
 * Achtung: das Gateway teilt sich SDOclient[0] mit cb_co_sdo_read/
 * _write/_write_stream - GTWA-SDO-Kommandos nicht gleichzeitig mit
 * eigenen SDO-Transfers absetzen (der ndjson-Dispatcher serialisiert
 * das; zusaetzliche Aufrufer muessen selbst darauf achten). */

/* Antwortdaten-Senke; Rueckgabe: verarbeitete Bytes (== len, wenn die
 * Senke alles uebernimmt; weniger haelt die Ausgabe im Gateway an). */
typedef size_t (*cb_co_gtwa_read_t)(void* user, const char* buf, size_t len);

/* Senke registrieren (vor oder nach cb_co_start). */
void cb_co_gtwa_set_read(cb_co_gtwa_read_t cb, void* user);

/* Kommandotext einspeisen. Rueckgabe: uebernommene Bytes (0, wenn die
 * Kommando-Fifo voll ist - spaeter erneut versuchen) oder -1, wenn
 * kein Gateway laeuft. */
int cb_co_gtwa_write(const char* buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* CB_CO_NODE_H_ */
