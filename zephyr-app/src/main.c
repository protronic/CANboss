/**
 * main.c
 *
 * Einstieg der CANbossTouch-Zephyr-App (Port des STM32-Builds):
 *
 *  - LVGL initialisiert das Zephyr-Modul selbst (SYS_INIT, Display aus
 *    dem Devicetree: SDL-Fenster auf native_sim, Panel auf Hardware)
 *  - CANopenNode-Stack (Node 127, App/OD) ueber co_zephyr.c
 *  - SDO-Worker fuer die UI-Datenpunkte (canboss_sdo_zephyr.c)
 *  - EDS-generierte LVGL-Screens (App/canboss_ui.c + App/generated/)
 *  - optional Berry-Scripting mit od-Modul + Shell-Kommando "berry"
 *
 * Der Main-Thread faehrt anschliessend die LVGL-Timer-Schleife.
 */

#include <stdio.h>

#include <zephyr/kernel.h>

#include "lvgl/lvgl.h"

#include "canboss_sdo.h"
#include "canboss_ui.h"
#include "co_zephyr.h"

#ifdef CONFIG_CANBOSSTOUCH_BERRY
#include "canboss_berry.h"
#endif

int
main(void) {
    if (cb_co_start(CONFIG_CANBOSSTOUCH_NODE_ID) != 0) {
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
