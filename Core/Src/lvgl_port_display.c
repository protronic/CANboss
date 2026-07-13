/*********************
 *      INCLUDES
 *********************/

#include "lvgl_port_display.h"
#include "main.h"
#include "ft81x.h"

/**********************
 *      DEFINES
 **********************/

/*
 * LVGL rendert im PARTIAL-Modus in zwei SRAM-Streifenpuffer; der
 * Flush-Callback schiebt jedes Dirty-Rechteck per SPI in das RAM_G des
 * FT813 (das eine statische Displayliste dauerhaft auf das Panel scannt).
 * 40 Zeilen x 800 Pixel x RGB565 = 64000 B je Puffer — passt als ein
 * einzelner SPI-Burst (HAL-Laengenlimit 65535).
 */
#define BUF_STRIPE_LINES 40
#define BUF_BYTES (MY_DISP_HOR_RES * BUF_STRIPE_LINES * 2)

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);

/**********************
 *  STATIC VARIABLES
 **********************/

static uint8_t buf_1[BUF_BYTES];
static uint8_t buf_2[BUF_BYTES];

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lvgl_display_init (void)
{
	/* ft81x_init() ist bereits in main() gelaufen (Panel scannt) */
	lv_display_t *disp = lv_display_create(MY_DISP_HOR_RES, MY_DISP_VER_RES);
	lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
	lv_display_set_buffers(disp, buf_1, buf_2, BUF_BYTES, LV_DISPLAY_RENDER_MODE_PARTIAL);
	lv_display_set_flush_cb(disp, flush_cb);
	lv_display_set_default(disp);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
	if (area->x2 >= area->x1 && area->y2 >= area->y1)
	{
		uint16_t x = (uint16_t)area->x1;
		uint16_t y = (uint16_t)area->y1;
		uint16_t w = (uint16_t)(area->x2 - area->x1 + 1);
		uint16_t h = (uint16_t)(area->y2 - area->y1 + 1);

		/* LVGL liefert little-endian RGB565 — Byte-fuer-Byte identisch zum
		 * FT81x-RGB565-Bitmapformat, direkt per SPI raus. */
		(void)ft81x_blit(x, y, w, h, px_map);
	}

	lv_display_flush_ready(disp);
}
