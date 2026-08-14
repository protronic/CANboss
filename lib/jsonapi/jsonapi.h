/**
 * jsonapi.h
 *
 * NDJSON-Interface fuer Webapps (WebSerial ueber USB-CDC, Web-BLE,
 * WebSocket-Bridge, ...): pro Zeile ein JSON-Objekt, in beide
 * Richtungen. Transport-agnostisch - die App liefert eine Byte-Senke
 * und speist empfangene Bytes ein.
 *
 * Requests (Webapp -> Geraet):
 *
 *   {"gtwa": ["0 preop", "[1] 1 w 0x2500 2 u32 123", "[2] 1 r 0x2500 2 u32"]}
 *       CiA-309-3-ASCII-Kommandos (CANopenNode-Gateway); auch als
 *       Einzelstring {"gtwa": "..."} moeglich. Antwortzeilen kommen
 *       asynchron als {"gtwa": ["[1] OK", ...]}.
 *
 *   {"mon": {"add": [385, "0x281"], "del": [...], "clear": true, "rate": 100}}
 *       PDO/COB-Monitor: COB-IDs (11 Bit) abonnieren; Frames kommen als
 *       {"pdo": {"cob": 385, "d": "11223344", "t": 123456}} (t = ms).
 *       rate = Mindestabstand je COB in ms (Default 100, 0 = jeder Frame).
 *
 *   {"fw": {"op": "begin", "slot": 0, "size": 12345, "name": "app.bin"}}
 *   {"fw": {"op": "data", "b64": "<Base64, max ~3 KiB je Zeile>"}}
 *   {"fw": {"op": "end", "crc32": "aabbccdd"}}
 *   {"fw": {"op": "list"}} | {"op":"erase","slot":0} | {"op":"abort"}
 *   {"fw": {"op": "send", "slot": 0, "node": 16, "index": "0x1F50", "sub": 1}}
 *       Firmware-Annahme + SDO-Streaming zum Knoten; Fortschritt kommt
 *       als {"fw": {"prog": [sent, total], "slot": 0, "node": 16}}.
 *
 *   {"ping": <x>} -> {"pong": <x>}   {"info": true} -> Limits/Version
 *
 * Antworten/Events (Geraet -> Webapp), je eine NDJSON-Zeile:
 *   {"gtwa":[...]}  {"pdo":{...}}  {"fw":{...}}  {"err":"..."}
 *
 * Threading: jsonapi_input() darf aus beliebigen Threads/Callbacks
 * kommen (Ringpuffer); die gesamte Verarbeitung inkl. aller Ausgaben
 * an die Senke laeuft in jsonapi_poll() - zyklisch aus einem
 * App-Worker rufen (z.B. alle 10 ms). Ein "fw send" blockiert poll()
 * fuer die Dauer des Transfers; PDO-Events puffern derweil.
 */

#ifndef CB_JSONAPI_H_
#define CB_JSONAPI_H_

#include <stddef.h>
#include <stdint.h>

#include "jsonapi_fw.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Muss alle Bytes uebernehmen (blockieren erlaubt); wird nur aus
     * jsonapi_poll() gerufen. */
    void (*write)(void* user, const void* buf, size_t len);
    void* user;
} jsonapi_sink_t;

/* Initialisieren und am co_node-Gateway (GTWA) sowie am Raw-RX-Hook
 * (PDO-Monitor) anmelden. fw_ops darf NULL sein (fw-Kommandos melden
 * dann einen Fehler). Vor cb_co_start() oder danach - beides ok. */
int jsonapi_init(const jsonapi_sink_t* sink, const jsonapi_fw_ops_t* fw_ops);

/* Empfangene Bytes vom Transport einspeisen (beliebiger Kontext). */
void jsonapi_input(const void* buf, size_t len);

/* Verarbeitung: Requests ausfuehren, GTWA-Antworten und PDO-Events
 * ausgeben. Zyklisch rufen. */
void jsonapi_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* CB_JSONAPI_H_ */
