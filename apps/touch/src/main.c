/**
 * main.c
 *
 * Einstieg der CANbossTouch-Zephyr-App (Port des STM32-Builds):
 *
 *  - LVGL initialisiert das Zephyr-Modul selbst (SYS_INIT, Display aus
 *    dem Devicetree: SDL-Fenster auf native_sim, Panel auf Hardware)
 *  - CANopenNode-Stack (Node 127, lib/od/canboss_master) ueber lib/canopen
 *  - SDO-Worker fuer die UI-Datenpunkte (canboss_sdo_zephyr.c)
 *  - EDS-generierte LVGL-Screens (App/canboss_ui.c + App/generated/)
 *  - optional Berry-Scripting mit od-Modul + Shell-Kommando "berry"
 *
 * Der Main-Thread faehrt anschliessend die LVGL-Timer-Schleife.
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>

#include "lvgl/lvgl.h"

#include "canboss_poc.h"
#include "canboss_sdo.h"
#include "canboss_ui.h"
#include "co_node.h"
#include "canboss_master.h" /* nach co_node.h (CANopenNode-Typen) */

#ifdef CONFIG_CANBOSSTOUCH_BERRY
#include "canboss_berry.h"
#endif

int
main(void) {
    /* OD-Konfiguration des Panels (muss den Stack ueberleben) */
    static CO_config_t cb_master_config;
    memset(&cb_master_config, 0, sizeof(cb_master_config));
    canboss_master_INIT_CONFIG(cb_master_config);

    /* Raw-Frames der PoC-Hallenlichtsteuerung aus dem RX-Thread */
    cb_co_set_raw_rx_hook(canboss_poc_can_rx);

    const cb_can_backend_t* backend = cb_can_backend_find("zephyr");
    if (cb_co_start(canboss_master, &cb_master_config, backend, "zephyr,canbus", 0, CONFIG_CANBOSSTOUCH_NODE_ID)
        != 0) {
        printf("Warnung: %s - UI laeuft ohne CAN weiter\n", cb_co_error());
    }

    canboss_sdo_init();

#ifdef CONFIG_CANBOSSTOUCH_BERRY
    canboss_berry_init();
#endif

    canboss_ui_init();

    for (;;) {
        uint32_t sleep_ms = lv_timer_handler();
        if (sleep_ms == LV_NO_TIMER_READY || sleep_ms > 50u) {
            sleep_ms = 50u;
        }
        if (sleep_ms < 1u) {
            sleep_ms = 1u;
        }
        k_msleep((int32_t)sleep_ms);
    }

    return 0;
}
