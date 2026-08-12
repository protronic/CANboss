/**
 * node_app.h
 *
 * Gemeinsamer Rahmen der Demo-Knoten (apps/nodes/demo_*): CANopenNode-
 * Slave (Heartbeat, SDO-Server, PDOs aus dem generierten OD) plus eine
 * zyklische Prozesswert-Simulation, damit auf dem Bus (vcan) etwas
 * Beobachtbares passiert.
 */

#ifndef CB_NODE_APP_H_
#define CB_NODE_APP_H_

#include <stdint.h>

#include "co_node.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Default-Simulationsintervall, wenn interval_ms_fn NULL ist */
#define CB_NODE_SIM_INTERVAL_MS 100u

/* Stack starten und Simulationsschleife fahren (kehrt nicht zurueck).
 * sim wird unter CO_LOCK_OD mit der Gesamtlaufzeit in ms aufgerufen und
 * schreibt direkt in die OD-RAM-Strukturen (TPDOs senden per Event-Timer
 * aus der EDS). interval_ms_fn liefert das aktuelle Intervall (z.B. aus
 * OD 0x2303); NULL = CB_NODE_SIM_INTERVAL_MS. */
void node_app_run(const char *name, OD_t *od, const CO_config_t *config, uint8_t node_id,
		  void (*sim)(uint32_t uptime_ms), uint32_t (*interval_ms_fn)(void));

#ifdef __cplusplus
}
#endif

#endif /* CB_NODE_APP_H_ */
