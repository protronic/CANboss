/**
 * canboss_berry.h
 *
 * Berry-Scripting der CANboss-Apps: eine Berry-VM mit nativen
 * od_*-Funktionen fuer den Zugriff auf das CANopen-Netzwerk und das
 * eigene Objektverzeichnis (inkl. der per RPDO empfangenen PDO-Werte).
 * Gemeinsame Schicht in lib/berry_od - das Touch-Panel haengt sie an
 * das Shell-Kommando "berry", der Monitor an die interaktive REPL
 * (Taste 's', siehe canboss_berry_repl()).
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
 *   od_nodes()                             Liste der bekannten Node-IDs
 *   help()                                 Beispiele und od_*-API anzeigen
 *
 * Einbindung: co_node.h VOR diesem Header einbinden (OD_t kommt aus
 * CANopenNode und laesst sich nicht vorwaerts deklarieren).
 */

#ifndef CANBOSS_BERRY_H_
#define CANBOSS_BERRY_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* VM anlegen und od_*-Funktionen registrieren. local_od ist das eigene
 * Objektverzeichnis fuer od_local* (z.B. canboss_master); NULL erlaubt,
 * dann melden od_local*-Aufrufe einen Fehler. */
void canboss_berry_init(OD_t* local_od);

/* Nur die od_*-Funktionen (inkl. od_nodes, ohne help) in eine bereits
 * bestehende VM registrieren - fuer Apps, die ihre eigene VM/REPL
 * mitbringen (z.B. canBLEberry auf der BLEberry-REPL). local_od wie
 * bei canboss_berry_init(). */
void canboss_berry_register(void* vm /* bvm* */, OD_t* local_od);

/* Node-IDs fuer od_nodes() hinterlegen (z.B. aus der generierten
 * Netzwerk-Registry der App). */
void canboss_berry_set_nodes(const uint8_t* ids, uint16_t count);

/* Skripttext ausfuehren; Ausgabe (print/Fehler) geht an die aktuelle
 * Senke. Rueckgabe 0 bei Erfolg. Thread-sicher (Mutex). */
int canboss_berry_exec(const char* code);

/* Ausgabesenke der VM (siehe berry_port.c) */
typedef void (*cb_berry_sink_t)(void* user, const char* buf, size_t len);
void cb_berry_set_sink(cb_berry_sink_t sink, void* user);

/* Interaktive REPL (berry_repl.c): liest/schreibt ueber die beiden
 * Callbacks (Signaturen passend zu tui_io_getc/tui_io_write des
 * Monitors). Blockiert bis exit/Strg-D. Terminal muss im Raw-Mode sein. */
typedef struct {
    int (*getc_fn)(int timeout_ms);              /* -1 = nichts anliegend */
    void (*write_fn)(const char* buf, size_t len);
} cb_berry_repl_io_t;
void canboss_berry_repl(const cb_berry_repl_io_t* io);

/* intern: VM-Handle (bvm*) fuer berry_repl.c */
void* canboss_berry_vm(void);

#ifdef __cplusplus
}
#endif

#endif /* CANBOSS_BERRY_H_ */
