/**
 * ui.h
 *
 * Terminal-UI des CANboss-Monitors (Port von CANboss-rs/src/ui.rs).
 *
 * Screens:
 *   - Netzwerk-Browser: Knoten aus der generierten Registry
 *     (ersetzt Setup-/EDS-Discovery des Rust-Originals; die Knotenliste
 *     kommt aus eds/network.json statt aus CouchDB)
 *   - Parameter-Monitor: Objekte links, Datenpunkte rechts,
 *     Lesen/Schreiben per SDO, Filter und Auto-Refresh
 */

#ifndef CB_UI_H_
#define CB_UI_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* UI starten. start_node_id > 0 springt direkt in den Monitor des
 * Knotens mit dieser Node-ID (--node). Rueckgabe 0 bei Erfolg. */
int cb_ui_run(int start_node_id, const char* can_label);

#ifdef __cplusplus
}
#endif

#endif /* CB_UI_H_ */
