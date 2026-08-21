/**
 * main_zephyr.c
 *
 * Einstieg des Zephyr-Builds (west build -b native_sim/native/64).
 *
 * Anders als der POSIX-Build (src/main.c, getopt) gibt es keine
 * Kommandozeile: das CAN-Geraet kommt aus dem Devicetree (chosen
 * zephyr,canbus), die eigene Node-ID aus CONFIG_CANBOSS_NODE_ID.
 * Schlaegt der CAN-Start fehl, laeuft die UI im Offline-Modus -
 * gleiches Verhalten wie das Rust-Original.
 */

#include <stdio.h>
#include <string.h>

#ifdef CONFIG_ARCH_POSIX
#include <nsi_main.h> /* nsi_exit(): native_sim-Prozess beenden */
#endif

#include "canboss.h"
#include "can_if.h"
#include "co_node.h"
#include "canboss_master.h" /* nach co_node.h (CANopenNode-Typen) */
#include "canboss_net.h"
#include "ui.h"

#ifdef CANBOSS_BERRY
#include "canboss_berry.h"
#endif

int
main(void) {
    const cb_can_backend_t* backend = cb_can_backend_find("zephyr");

    /* OD-Konfiguration des Masters (muss den Stack ueberleben) */
    static CO_config_t cb_master_config;
    memset(&cb_master_config, 0, sizeof(cb_master_config));
    OD_INIT_CONFIG(cb_master_config);

    if (cb_co_start(OD, &cb_master_config, backend, "zephyr,canbus", 0, CONFIG_CANBOSS_NODE_ID) != 0) {
        printf("Warnung: %s - starte im Offline-Modus\r\n", cb_co_error());
    }

#ifdef CANBOSS_BERRY
    canboss_berry_init(OD);
    {
        uint8_t ids[64];
        uint16_t n = cb_net_node_count < 64 ? cb_net_node_count : 64;
        for (uint16_t i = 0; i < n; i++) {
            ids[i] = cb_net_nodes[i]->node_id;
        }
        canboss_berry_set_nodes(ids, n);
    }
#endif

    (void)cb_ui_run(0, "zephyr/can");

    cb_co_stop();

#ifdef CONFIG_ARCH_POSIX
    nsi_exit(0); /* sonst laeuft der Simulator nach 'q' weiter */
#endif
    return 0;
}
