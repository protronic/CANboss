/**
 * main_host.c
 *
 * Linux/SDL2-Host-Build der CANbossTouch-Oberflaeche (Vorbild:
 * examples/oxivgl-host im embassy-Repo). Laesst die komplette LVGL-UI
 * (EDS-Screens + PoC-Hallenlicht) am Entwicklungs-PC laufen:
 *
 *  - Display/Maus: LVGL-SDL-Treiber, 800x480-Fenster
 *  - SDO: Mock (host/canboss_sdo_host.c) mit simulierten Datenpunkten
 *  - PoC-CAN: SocketCAN (host/canboss_poc_can_host.c), Default vcan0
 *
 * Bauen und starten (SDL2-Devel-Pakete + pkg-config noetig):
 *
 *   make host
 *   ./build/host/canboss_host [can-interface]
 *
 * Fuer die PoC-CAN-Frames vorher (optional):
 *
 *   sudo modprobe vcan
 *   sudo ip link add dev vcan0 type vcan
 *   sudo ip link set vcan0 up
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lvgl/lvgl.h"
#include "canboss_ui.h"
#include "canboss_poc.h"
#include "canboss_sdo.h"
#include "canboss_host.h"

static uint32_t
host_tick_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

/* Aktiven Screen als 24-Bit-BMP speichern (lv_snapshot, ARGB8888 -> BGR) */
static void
host_save_screen_bmp(const char* path) {
    lv_draw_buf_t* snap = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_ARGB8888);
    if (snap == NULL) {
        fprintf(stderr, "canboss_host: Snapshot fehlgeschlagen\n");
        return;
    }

    uint32_t w = snap->header.w;
    uint32_t h = snap->header.h;
    uint32_t row_out = (w * 3u + 3u) & ~3u; /* BMP-Zeilen auf 4 Byte gerundet */
    uint32_t data_size = row_out * h;
    uint32_t file_size = 54u + data_size;

    FILE* f = fopen(path, "wb");
    if (f == NULL) {
        perror("canboss_host: BMP");
        lv_draw_buf_destroy(snap);
        return;
    }

    uint8_t hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    lv_memcpy(&hdr[2], &file_size, 4);
    hdr[10] = 54;                 /* Pixeldaten-Offset */
    hdr[14] = 40;                 /* BITMAPINFOHEADER */
    lv_memcpy(&hdr[18], &w, 4);
    lv_memcpy(&hdr[22], &h, 4);
    hdr[26] = 1;                  /* Planes */
    hdr[28] = 24;                 /* Bits/Pixel */
    lv_memcpy(&hdr[34], &data_size, 4);
    fwrite(hdr, 1, sizeof(hdr), f);

    uint8_t* row = malloc(row_out);
    for (int32_t y = (int32_t)h - 1; y >= 0; y--) { /* BMP: bottom-up */
        const uint8_t* src = snap->data + (uint32_t)y * snap->header.stride;
        for (uint32_t x = 0; x < w; x++) {
            row[x * 3u + 0] = src[x * 4u + 0]; /* B */
            row[x * 3u + 1] = src[x * 4u + 1]; /* G */
            row[x * 3u + 2] = src[x * 4u + 2]; /* R */
        }
        memset(row + w * 3u, 0, row_out - w * 3u);
        fwrite(row, 1, row_out, f);
    }
    free(row);
    fclose(f);
    lv_draw_buf_destroy(snap);
}

int
main(int argc, char** argv) {
    const char* can_if = (argc > 1) ? argv[1] : "vcan0";

    lv_init();
    lv_tick_set_cb(host_tick_ms); /* der SDL-Treiber ersetzt das durch SDL_GetTicks */

    lv_display_t* disp = lv_sdl_window_create(800, 480);
    if (disp == NULL) {
        fprintf(stderr, "canboss_host: SDL-Fenster konnte nicht erstellt werden\n");
        return 1;
    }
    lv_sdl_window_set_title(disp, "CANbossTouch Host");
    lv_indev_t* mouse = lv_sdl_mouse_create();
    lv_indev_set_display(mouse, disp);

    canboss_sdo_init(); /* Mock: simulierte Datenpunkte */
    if (canboss_poc_can_host_init(can_if) == 0) {
        printf("PoC-CAN auf %s aktiv\n", can_if);
    } else {
        printf("PoC-CAN: %s nicht verfuegbar — UI ohne CAN\n", can_if);
    }

    canboss_ui_init();

    /* Headless-Betrieb (CI/Vorschau): CANBOSS_HOST_SCREENSHOT=<prefix>
     * rendert nacheinander Hauptmenue, alle Knoten-Screens und den
     * PoC-Hallenscreen (je 2 s), schreibt je ein BMP und beendet sich. */
    const char* shot_prefix = getenv("CANBOSS_HOST_SCREENSHOT");

    uint32_t started = lv_tick_get();
    int shot_stage = 0;

    for (;;) {
        uint32_t wait_ms = lv_timer_handler();
        if (wait_ms == LV_NO_TIMER_READY || wait_ms > 10u) {
            wait_ms = 10u;
        }
        usleep(wait_ms * 1000u);

        if (shot_prefix != NULL && lv_tick_elaps(started) > 2000u) {
            char path[256];
            snprintf(path, sizeof(path), "%s_%d.bmp", shot_prefix, shot_stage);
            host_save_screen_bmp(path);
            printf("Screenshot: %s\n", path);

            /* Knoten-Screens: zusaetzlich das Listenende festhalten
             * (Screen-Layout: Header, scrollbare Liste, Tastatur) */
            lv_obj_t* scr = lv_screen_active();
            if (shot_stage >= 1 && shot_stage <= canboss_node_count && lv_obj_get_child_count(scr) >= 2) {
                lv_obj_scroll_to_y(lv_obj_get_child(scr, 1), LV_COORD_MAX, LV_ANIM_OFF);
                lv_refr_now(NULL);
                snprintf(path, sizeof(path), "%s_%db.bmp", shot_prefix, shot_stage);
                host_save_screen_bmp(path);
                printf("Screenshot: %s\n", path);
            }

            if (shot_stage < canboss_node_count) {
                canboss_nodes[shot_stage]->screen_create();
            } else if (shot_stage == canboss_node_count) {
                canboss_poc_screen_create();
            } else {
                return 0;
            }
            shot_stage++;
            started = lv_tick_get();
        }
    }
    return 0;
}
