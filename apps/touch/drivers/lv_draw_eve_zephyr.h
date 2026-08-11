/**
 * lv_draw_eve_zephyr.h
 *
 * Zephyr-SPI/GPIO-Glue fuer LVGL DRAW_EVE auf dem gen4-FT813.
 */

#ifndef CANBOSS_LV_DRAW_EVE_ZEPHYR_H
#define CANBOSS_LV_DRAW_EVE_ZEPHYR_H

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialisiert SPI/GPIO vom DT-Knoten ft813, legt das EVE-Display und
 * den Touch-Indev an und setzt das Display als Default.
 *
 * @return 0 bei Erfolg, negativer errno sonst
 */
int canboss_eve_display_init(void);

/** Das von canboss_eve_display_init() erzeugte LVGL-Display (oder NULL). */
lv_display_t *canboss_eve_display_get(void);

#ifdef __cplusplus
}
#endif

#endif /* CANBOSS_LV_DRAW_EVE_ZEPHYR_H */
