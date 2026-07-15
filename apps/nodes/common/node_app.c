/**
 * node_app.c
 *
 * Gemeinsamer Rahmen der Demo-Knoten: Stack ueber lib/canopen starten
 * (RX-/Mainline-Thread laufen dort), hier nur noch die
 * Simulationsschleife im Main-Thread.
 */

#include "node_app.h"

#include <stdio.h>

#include <zephyr/kernel.h>

#include "osal.h"

void
node_app_run(const char* name, OD_t* od, const CO_config_t* config, uint8_t node_id, void (*sim)(uint32_t uptime_ms)) {
    const cb_can_backend_t* backend = cb_can_backend_find("zephyr");

    if (cb_co_start(od, config, backend, "zephyr,canbus", 0, node_id) != 0) {
        printf("%s: %s - Knoten laeuft ohne CAN (nur Simulation)\n", name, cb_co_error());
    } else {
        printf("%s: CANopen-Knoten %u laeuft (Heartbeat + SDO-Server + PDOs)\n", name, node_id);
    }

    uint32_t uptime_ms = 0;
    for (;;) {
        k_msleep(CB_NODE_SIM_INTERVAL_MS);
        uptime_ms += CB_NODE_SIM_INTERVAL_MS;

        if (sim != NULL) {
            CO_t* co = cb_co_handle();
            if (co != NULL) {
                CO_LOCK_OD(co->CANmodule);
            }
            sim(uptime_ms);
            if (co != NULL) {
                CO_UNLOCK_OD(co->CANmodule);
            }
        }
    }
}
