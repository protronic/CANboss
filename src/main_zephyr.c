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

#ifdef CONFIG_ARCH_POSIX
#include <nsi_main.h> /* nsi_exit(): native_sim-Prozess beenden */
#endif

#include "canboss.h"
#include "can_if.h"
#include "co_node.h"
#include "ui.h"

int
main(void) {
    const cb_can_backend_t* backend = cb_can_backend_find("zephyr");

    if (cb_co_start(backend, "zephyr,canbus", 0, CONFIG_CANBOSS_NODE_ID) != 0) {
        printf("Warnung: %s - starte im Offline-Modus\r\n", cb_co_error());
    }

    (void)cb_ui_run(0, "zephyr/can");

    cb_co_stop();

#ifdef CONFIG_ARCH_POSIX
    nsi_exit(0); /* sonst laeuft der Simulator nach 'q' weiter */
#endif
    return 0;
}
