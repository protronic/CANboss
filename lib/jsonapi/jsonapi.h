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
 *   {"gtwa": ["help", "[1] 1 w 0x2500 2 u32 123", "[2] 1 r 0x2500 2 u32"]}
 *       CiA-309-3-ASCII-Kommandos (CANopenNode-Gateway); auch als
 *       Einzelstring {"gtwa": "help"} moeglich. Fehlt "["<seq>"]",
 *       haengt jsonapi sie an. Jede Antwort ist ein Objekt mit der
 *       Sequenz als Key: {"gtwa": {"1": "OK"}} bzw. Zahl unquoted
 *       {"gtwa": {"2": 123}}. Ein Array wird in Batches zu je batmax
 *       Kommandos gesplittet, jede Teilausgabe eine Zeile
 *       {"gtwa": {"1": "OK", "2": 123}}. Default batmax=1; setzen mit
 *       {"gtwa": {"batmax": n}} (1..16), steht auch in {"info":…}.
 *       Autozähler: {"gtwa": {"seq": n}} setzt die naechste zu
 *       vergebende Nummer ohne "["seq"]" (0..9999, danach Wrap).
 *       Zeilen ohne [seq] (help) bleiben String: {"gtwa": "…"}.
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
 *   {"repl": "1+2"}  bzw.  {"repl": ["led(true)", "millis()"]}
 *       Berry-Code ausfuehren (optional, jsonapi_set_repl()): Ausgabe
 *       kommt zeilenweise als {"repl": ["3"]}, den Abschluss meldet
 *       {"repl": {"ok": true|false}}.
 *
 *   {"ping": <x>} -> {"pong": <x>}   {"info": true} -> Limits/Version
 *
 *   {"nodstat": 0}   -> {"nodstat": [{"7": "op"}, {"13": "pre"}, ...]}
 *   {"nodstat": 13}  -> {"nodstat": [{"13": "pre"}]}
 *       NMT-State der per Heartbeat-Consumer (0x1016) gesehenen Knoten.
 *       0 = alle, 1..127 = nur diese Node-ID. Ein Knoten gilt als
 *       ueberwacht, sobald von ihm ein Heartbeat empfangen wurde
 *       (HBstate ACTIVE). Value ist der NMT-State als String:
 *       "init" (0), "stop" (4), "op" (5), "pre" (127), "n/a" (-1).
 *       Ohne Stack: {"err":"nodstat: CANopen offline"}. Ohne Eintrag
 *       in 0x1016: {"err":"nodstat: kein Heartbeat-Consumer konfiguriert"}.
 *
 * Zeilen ohne fuehrende '{' sind SLCAN-ASCII (Lawicel: O, C, t..., r...,
 * Z, V, N, F, ...) und gehen als Raw-CAN parallel zum CANopen-Verkehr
 * auf denselben Bus; empfangene Frames kommen bei offenem Kanal als
 * "t..."-Zeilen zurueck. Details: jsonapi_slcan.h. Der Port laesst
 * sich damit auch direkt an slcand/python-can haengen.
 *
 * Antworten/Events (Geraet -> Webapp), je eine NDJSON-Zeile:
 *   {"gtwa":{...}}  {"pdo":{...}}  {"fw":{...}}  {"repl":...}  {"nodstat":[...]}
 *   {"err":"..."}
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

/* --- optionale Berry-REPL ------------------------------------------
 *
 * Executor fuer {"repl": ...}: fuehrt code synchron aus (laeuft im
 * poll-Thread; braucht dort entsprechend Stack fuer den
 * Berry-Compiler). Ausgabetext waehrend der Ausfuehrung ueber
 * jsonapi_repl_out() liefern - typisch, indem die Berry-Senke
 * (cb_berry_set_sink bzw. be_writebuffer-Routing der App) dorthin
 * zeigt. Rueckgabe 0 = ok. */
typedef int (*jsonapi_repl_exec_t)(void* user, const char* code);

/* Executor registrieren; NULL deaktiviert {"repl": ...} wieder. */
void jsonapi_set_repl(jsonapi_repl_exec_t exec, void* user);

/* Ausgabetext des gerade laufenden repl-Kommandos (nur aus dem
 * Executor-Kontext rufen); wird zeilenweise als {"repl":["..."]}
 * ausgegeben. */
void jsonapi_repl_out(const char* buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* CB_JSONAPI_H_ */
