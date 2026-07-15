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

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>

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

#ifdef CB_DEBUG_OPEN_NODE
/* Monkey-Tester: simuliert Maus-Eingaben ueber den Zephyr-Input-Pfad
 * (SDL-Touch-Device -> lvgl_pointer -> Indev). Tippt/zieht zufaellig
 * auf echte Widgets (Listeneintraege, Back-Button, Slider, ...), damit
 * Navigation + Widget-Interaktion dauerhaft durchlaufen werden. */
#include <zephyr/input/input.h>
#include <lvgl_mem.h>
#include <zephyr/sys/sys_heap.h>

static uint32_t cb_dbg_seed = 0x2f6e2b1u;

static uint32_t
cb_dbg_rand(void) {
    cb_dbg_seed = cb_dbg_seed * 1664525u + 1013904223u;
    return cb_dbg_seed >> 8;
}

static void
cb_debug_touch(int x, int y, int pressed) {
    const struct device* tdev = DEVICE_DT_GET(DT_NODELABEL(input_sdl_touch));
    input_report_abs(tdev, INPUT_ABS_X, x, false, K_FOREVER);
    input_report_abs(tdev, INPUT_ABS_Y, y, false, K_FOREVER);
    input_report_key(tdev, INPUT_BTN_TOUCH, pressed, true, K_FOREVER);
}

/* Zufaelligen (Ur-)Enkel des aktiven Screens auswaehlen */
static lv_obj_t*
cb_debug_pick(lv_obj_t* obj, int depth) {
    uint32_t n = lv_obj_get_child_count(obj);
    if (n == 0 || depth > 7 || (cb_dbg_rand() % 5u) == 0u) {
        return obj;
    }
    return cb_debug_pick(lv_obj_get_child(obj, (int32_t)(cb_dbg_rand() % n)), depth + 1);
}

static void
cb_debug_nav_timer(lv_timer_t* t) {
    static int pressed = 0;
    static int px, py;
    static uint32_t taps = 0;
    lv_obj_t* scr = lv_screen_active();

    if (!pressed) {
        if ((cb_dbg_rand() % 4u) != 0u) { /* 75%: gezielt ein Widget treffen */
            lv_obj_t* target = cb_debug_pick(scr, 0);
            lv_area_t a;
            lv_obj_get_coords(target, &a);
            px = (a.x1 + a.x2) / 2;
            py = (a.y1 + a.y2) / 2;
        } else { /* 25%: freie Position */
            px = (int)(cb_dbg_rand() % (uint32_t)lv_obj_get_width(scr));
            py = (int)(cb_dbg_rand() % (uint32_t)lv_obj_get_height(scr));
        }
        cb_debug_touch(px, py, 1);
        pressed = 1;
        if ((++taps % 50u) == 0u) {
            struct sys_memory_stats st = {0};
            lvgl_heap_stats(&st);
            printf("dbg-monkey: %u taps, lvgl-heap: used=%zu free=%zu max=%zu\n", taps, st.allocated_bytes,
                   st.free_bytes, st.max_allocated_bytes);
            fflush(stdout);
        }
    } else {
        if ((cb_dbg_rand() % 3u) == 0u) { /* 1/3: ziehen statt loslassen */
            px += (int)(cb_dbg_rand() % 121u) - 60;
            py += (int)(cb_dbg_rand() % 121u) - 60;
            if (px < 0) { px = 0; }
            if (py < 0) { py = 0; }
            cb_debug_touch(px, py, 1);
        } else {
            cb_debug_touch(px, py, 0);
            pressed = 0;
        }
    }
    (void)t;
}
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

    /* Ohne bereites Display hat lvgl_init() (SYS_INIT des LVGL-Moduls)
     * abgebrochen, BEVOR lv_init()/Heap-Init liefen - jeder lv_*-Aufruf
     * wuerde dann abstuerzen (auf native_sim z.B. wenn SDL kein Fenster
     * oeffnen kann). Klar melden statt SIGSEGV. */
    const struct device* display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display)) {
        printf("Fehler: Display nicht bereit (native_sim: SDL/DISPLAY pruefen) - UI startet nicht, CAN-Stack laeuft "
               "weiter\n");
        for (;;) {
            k_msleep(1000);
        }
    }

    /* Display einschalten: der SDL-Treiber (native_sim) praesentiert
     * erst nach blanking_off, am FT813 schaltet es das Backlight ein.
     * (Das LVGL-Modul selbst ruft blanking_off nicht auf.) */
    (void)display_blanking_off(display);

    canboss_ui_init();
#ifdef CB_DEBUG_OPEN_NODE /* Testpfad: Monkey-Eingaben headless simulieren */
    lv_timer_create(cb_debug_nav_timer, 60, NULL);
#endif

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
