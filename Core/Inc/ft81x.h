/**
 ******************************************************************************
 * @file    ft81x.h
 * @brief   Minimaler FT81x-(EVE2-)Hosttreiber ueber blockierendes SPI.
 *
 * Zielhardware: FT813 im 4D-Systems gen4-FT813-70CTP-CLB (800x480,
 * kapazitiver Touch) am SPI2 des STM32H573I-DK. Das EVE-IC wird als
 * "dummer Framebuffer" betrieben: der Host schreibt RGB565-Pixel (LVGL-
 * Streifenpuffer) in das 1-MiB-RAM_G, eine statische Displayliste scannt
 * die Bitmap dauerhaft auf das Panel. Touch liefert die eingebaute
 * Touch-Engine (REG_TOUCH_SCREEN_XY) ueber denselben SPI-Bus.
 *
 * C-Port des Rust-Treibers examples/gen4-ft813-70ctp-h573i-dk/src/ft81x.rs
 * aus dem embassy-Repo — Aenderungen dort nachziehen.
 ******************************************************************************
 */
#ifndef __FT81X_H__
#define __FT81X_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define FT81X_DISPLAY_WIDTH   800
#define FT81X_DISPLAY_HEIGHT  480
/* Framebuffer-Zeilenlaenge in Bytes (RGB565) */
#define FT81X_LINE_STRIDE     (FT81X_DISPLAY_WIDTH * 2)

typedef enum
{
	FT81X_OK = 0,
	FT81X_ERR_SPI,            /* SPI-Transfer fehlgeschlagen */
	FT81X_ERR_CHIP_ID,        /* REG_ID meldet nie 0x7C (Verdrahtung/Takt) */
	FT81X_ERR_RESET_TIMEOUT,  /* Grafik-Engine verlaesst den Reset nicht */
} ft81x_status_t;

typedef struct
{
	int32_t x;       /* Pixelposition, gueltig solange pressed */
	int32_t y;
	bool    pressed; /* true solange das Panel Kontakt meldet */
} ft81x_touch_t;

/* Power-up + Panel-Timings + leere Displayliste (Backlight bleibt aus).
 * SPI2 muss initialisiert sein und <= 11 MHz laufen. */
ft81x_status_t ft81x_init(void);

/* Nach ft81x_init(): SPI2 auf Betriebsgeschwindigkeit (250 MHz / 16 =
 * 15.625 MHz; FT81x-Maximum 30 MHz). */
void ft81x_set_spi_run_speed(void);

/* Backlight-Helligkeit 0..128 (FT813 REG_PWM_DUTY) */
ft81x_status_t ft81x_set_backlight(uint8_t duty);

/* Gesamten RAM_G-Framebuffer mit einer RGB565-Farbe fuellen */
ft81x_status_t ft81x_clear_framebuffer(uint16_t rgb565);

/* Statische Scanout-Displayliste aktivieren (einmalig; danach erscheinen
 * RAM_G-Schreibzugriffe mit dem naechsten Panel-Refresh) */
ft81x_status_t ft81x_show_framebuffer(void);

/* w*h-RGB565-Rechteck (zeilenweise, little-endian) an (x,y) in den
 * Framebuffer kopieren — der LVGL-Flush-Pfad */
ft81x_status_t ft81x_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *px);

/* Einen Touch-Messwert aus der kapazitiven Touch-Engine lesen */
ft81x_status_t ft81x_touch_read(ft81x_touch_t *point);

#ifdef __cplusplus
}
#endif

#endif /* __FT81X_H__ */
