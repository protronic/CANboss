/**
 * canboss_berry.h
 *
 * Berry-Scripting der CANbossTouch-App: eine Berry-VM mit nativen
 * od_*-Funktionen fuer den Zugriff auf das CANopen-Netzwerk und das
 * eigene Objektverzeichnis (inkl. der per RPDO empfangenen PDO-Werte).
 *
 * Skript-API (globale Funktionen):
 *
 *   od_read(node, index, sub)              SDO-Upload -> int (LE) oder string (>8 Byte)
 *   od_reads(node, index, sub)             SDO-Upload -> string
 *   od_readf(node, index, sub)             SDO-Upload REAL32 -> real
 *   od_write(node, index, sub, wert, size) SDO-Download int (size 1/2/4/8)
 *   od_write(node, index, sub, "text")     SDO-Download VISIBLE_STRING
 *   od_write(node, index, sub, 1.5)        SDO-Download REAL32
 *   od_local(index, sub)                   eigenes OD lesen -> int/string
 *   od_localf(index, sub)                  eigenes OD lesen REAL32 -> real
 *   od_local_write(index, sub, wert[,size])eigenes OD schreiben
 *   od_nodes()                             Liste der Node-IDs aus der Registry
 *
 * Shell: `berry <skript>` fuehrt Skripttext aus (Ausgabe an die Shell),
 * z.B.:  berry print(od_read(127, 0x1017, 0))
 */

#ifndef CANBOSS_BERRY_H_
#define CANBOSS_BERRY_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* VM anlegen, od_*-Funktionen registrieren, Shell-Kommando aktivieren. */
void canboss_berry_init(void);

/* Skripttext ausfuehren; Ausgabe (print/Fehler) geht an die aktuelle
 * Senke. Rueckgabe 0 bei Erfolg. Thread-sicher (Mutex). */
int canboss_berry_exec(const char* code);

/* Ausgabesenke der VM (siehe berry_port.c) */
typedef void (*cb_berry_sink_t)(void* user, const char* buf, size_t len);
void cb_berry_set_sink(cb_berry_sink_t sink, void* user);

#ifdef __cplusplus
}
#endif

#endif /* CANBOSS_BERRY_H_ */
