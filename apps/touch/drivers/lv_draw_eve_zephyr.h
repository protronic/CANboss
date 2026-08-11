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

/**
 * Serialisiert den EVE-SPI-Bus zwischen der LVGL-Schleife und den
 * eve-Shell-Kommandos. DRAW_EVE haelt CS ueber ganze Transaktionen
 * hinweg, darum muss jeder Nutzer den Bus exklusiv halten.
 */
void canboss_eve_lock(void);
void canboss_eve_unlock(void);

/**
 * lv_timer_handler() unter dem Bus-Lock, plus Mitschrift der
 * Displaylisten-Groesse und der Koprozessor-Faults ('eve status').
 *
 * @return Wartezeit in ms wie von lv_timer_handler()
 */
uint32_t canboss_eve_timer_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* CANBOSS_LV_DRAW_EVE_ZEPHYR_H */
