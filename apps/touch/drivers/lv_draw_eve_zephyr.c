/**
 * lv_draw_eve_zephyr.c
 *
 * Zephyr-Port von LVGL DRAW_EVE fuer das gen4-FT813-70CTP-CLB:
 *  - op_cb steuert PD_N, CS_N und SPI (ohne Zephyr-Auto-CS)
 *  - Panel-Timings wie der fruehere Framebuffer-Treiber
 *  - Goodix-GT911-Patch (FTDI AN_336) + Touch ueber lv_draw_eve_touch
 *
 * Das Zephyr-LVGL-Modul bindet parallel ein Dummy-Display (Chosen);
 * dieses Modul setzt das EVE-Display danach als Default.
 */

#include "lv_draw_eve_zephyr.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <lvgl/src/drivers/draw/eve/lv_draw_eve_display.h>
#include <lvgl/src/drivers/draw/eve/lv_draw_eve_display_defines.h>

LOG_MODULE_REGISTER(canboss_eve, CONFIG_DISPLAY_LOG_LEVEL);

#define FT813_NODE DT_NODELABEL(ft813)

#if !DT_NODE_HAS_STATUS(FT813_NODE, okay)
#error "DRAW_EVE braucht DT-Knoten ft813 (protronic,ft813) status okay"
#endif

/* Bring-up laut FT81x-Datenblatt <= 11 MHz */
#define FT813_SLOW_HZ 8000000u

/* Panel-Timings: 4D Systems gen4-FT813-70 (Standard-WVGA) */
#define H_CYCLE  928
#define H_OFFSET 88
#define H_SYNC0  0
#define H_SYNC1  48
#define V_CYCLE  525
#define V_OFFSET 32
#define V_SYNC0  0
#define V_SYNC1  3
#define PCLK_DIV 2
#define PCLK_POL 1
#define SWIZZLE  0
#define CSPREAD  0

struct canboss_eve_ctx {
	struct spi_dt_spec bus;
	struct spi_config spi_cfg; /* Kopie ohne Auto-CS */
	struct gpio_dt_spec pd;
	struct gpio_dt_spec cs;
	bool fast;
	lv_display_t *disp;
};

static struct canboss_eve_ctx eve_ctx;
static lv_display_t *eve_disp;

static void
eve_spi_set_hz(uint32_t hz)
{
	eve_ctx.spi_cfg.frequency = hz;
}

static int
eve_spi_send(const void *tx, size_t len)
{
	const struct spi_buf tx_buf = {.buf = (void *)tx, .len = len};
	const struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};

	return spi_write(eve_ctx.bus.bus, &eve_ctx.spi_cfg, &tx_set);
}

static int
eve_spi_recv(void *rx, size_t len)
{
	/* TX-NULL taktet Dummy-Bytes (Zephyr SPI) */
	const struct spi_buf tx_buf = {.buf = NULL, .len = len};
	const struct spi_buf rx_buf = {.buf = rx, .len = len};
	const struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};
	const struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1};

	return spi_transceive(eve_ctx.bus.bus, &eve_ctx.spi_cfg, &tx_set, &rx_set);
}

static void
eve_op_cb(lv_display_t *disp, lv_draw_eve_operation_t operation, void *data, uint32_t length)
{
	ARG_UNUSED(disp);

	switch (operation) {
	case LV_DRAW_EVE_OPERATION_POWERDOWN_SET:
		/* PD_N low-aktiv: logical 1 = Power-Down */
		(void)gpio_pin_set_dt(&eve_ctx.pd, 1);
		break;
	case LV_DRAW_EVE_OPERATION_POWERDOWN_CLEAR:
		(void)gpio_pin_set_dt(&eve_ctx.pd, 0);
		break;
	case LV_DRAW_EVE_OPERATION_CS_ASSERT:
		(void)gpio_pin_set_dt(&eve_ctx.cs, 1);
		break;
	case LV_DRAW_EVE_OPERATION_CS_DEASSERT:
		(void)gpio_pin_set_dt(&eve_ctx.cs, 0);
		break;
	case LV_DRAW_EVE_OPERATION_SPI_SEND:
		if (data != NULL && length > 0) {
			int ret = eve_spi_send(data, length);
			if (ret != 0) {
				LOG_ERR("SPI send failed (%d)", ret);
			}
		}
		break;
	case LV_DRAW_EVE_OPERATION_SPI_RECEIVE:
		if (data != NULL && length > 0) {
			int ret = eve_spi_recv(data, length);
			if (ret != 0) {
				LOG_ERR("SPI recv failed (%d)", ret);
				memset(data, 0, length);
			}
		}
		break;
	default:
		break;
	}
}

int
canboss_eve_display_init(void)
{
	int ret;

	if (eve_disp != NULL) {
		return 0;
	}

	if (!DT_NODE_HAS_PROP(FT813_NODE, pd_gpios)) {
		return -ENODEV;
	}

	eve_ctx.bus = (struct spi_dt_spec)SPI_DT_SPEC_GET(
		FT813_NODE, SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0);
	eve_ctx.pd = (struct gpio_dt_spec)GPIO_DT_SPEC_GET(FT813_NODE, pd_gpios);
	/* CS vom SPI-Controller (cs-gpios Index = Chip-Select des Childs) */
	eve_ctx.cs = (struct gpio_dt_spec)GPIO_DT_SPEC_GET_BY_IDX(DT_BUS(FT813_NODE), cs_gpios, 0);

	if (!spi_is_ready_dt(&eve_ctx.bus)) {
		LOG_ERR("SPI nicht bereit");
		return -ENODEV;
	}
	if (!gpio_is_ready_dt(&eve_ctx.pd) || !gpio_is_ready_dt(&eve_ctx.cs)) {
		LOG_ERR("PD/CS-GPIO nicht bereit");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&eve_ctx.pd, GPIO_OUTPUT_ACTIVE);
	if (ret != 0) {
		return ret;
	}
	ret = gpio_pin_configure_dt(&eve_ctx.cs, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		return ret;
	}

	/* SPI-Config ohne Auto-CS: DRAW_EVE haelt CS ueber mehrere Bursts */
	eve_ctx.spi_cfg = eve_ctx.bus.config;
	memset(&eve_ctx.spi_cfg.cs, 0, sizeof(eve_ctx.spi_cfg.cs));
	eve_spi_set_hz(FT813_SLOW_HZ);
	eve_ctx.fast = false;

	lv_draw_eve_parameters_t params = {
		.hor_res = DT_PROP(FT813_NODE, width),
		.ver_res = DT_PROP(FT813_NODE, height),
		.hcycle = H_CYCLE,
		.hoffset = H_OFFSET,
		.hsync0 = H_SYNC0,
		.hsync1 = H_SYNC1,
		.vcycle = V_CYCLE,
		.voffset = V_OFFSET,
		.vsync0 = V_SYNC0,
		.vsync1 = V_SYNC1,
		.swizzle = SWIZZLE,
		.pclkpol = PCLK_POL,
		.cspread = CSPREAD,
		.pclk = PCLK_DIV,
		.has_crystal = false, /* gen4: interner Oszillator */
		/* gen4-FT813-70CTP-CLB: Goodix GT911. Bei EVE2 laedt
		 * EVE_init() damit den FTDI-AN_336-Patch und setzt
		 * REG_TOUCH_CONFIG auf 0x05D0 (I2C-Adresse 0x5D). */
		.has_gt911 = true,
		.backlight_pwm = DT_PROP(FT813_NODE, backlight_duty),
		.backlight_freq = 250,
	};

	eve_disp = lv_draw_eve_display_create(&params, eve_op_cb, &eve_ctx);
	if (eve_disp == NULL) {
		LOG_ERR("lv_draw_eve_display_create fehlgeschlagen");
		return -EIO;
	}
	eve_ctx.disp = eve_disp;

	/* Nach EVE_init() auf Betriebsfrequenz */
	eve_spi_set_hz(eve_ctx.bus.config.frequency);
	eve_ctx.fast = true;

	/* Der GT911-Patch wurde bereits in EVE_init() geladen. Fuer LVGLs
	 * Single-Pointer-Indev Kompatibilitaetsmodus + Continuous aktivieren. */
	lv_draw_eve_memwrite8(eve_disp, LV_EVE_REG_CTOUCH_EXTENDED, 1);
	lv_draw_eve_memwrite8(eve_disp, LV_EVE_REG_TOUCH_MODE, 3);

	(void)lv_draw_eve_touch_create(eve_disp);
	lv_display_set_default(eve_disp);

	LOG_INF("DRAW_EVE FT813 + GT911 bereit (%ux%u @ %u Hz SPI, touch-config=0x%04x)",
		params.hor_res, params.ver_res, eve_ctx.spi_cfg.frequency,
		lv_draw_eve_memread16(eve_disp, LV_EVE_REG_TOUCH_CONFIG));
	return 0;
}

lv_display_t *
canboss_eve_display_get(void)
{
	return eve_disp;
}
