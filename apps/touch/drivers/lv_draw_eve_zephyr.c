/**
 * lv_draw_eve_zephyr.c
 *
 * Zephyr-Port von LVGL DRAW_EVE fuer das gen4-FT813-70CTP-CLB:
 *  - op_cb steuert PD_N, CS_N und SPI (ohne Zephyr-Auto-CS)
 *  - Panel-Timings wie der fruehere Framebuffer-Treiber
 *  - Touch ueber die native kapazitive Engine des FT813 (kein
 *    GT911-Patch!) mit eigenem Pointer-Indev: das gen4-Panel meldet
 *    die Achsen transponiert zum FT81x-Default
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
#include <zephyr/sys/printk.h>

#ifdef CONFIG_SHELL
#include <stdlib.h>
#include <zephyr/shell/shell.h>
#endif

#include <lvgl/lvgl.h>
#include <lvgl/src/display/lv_display_private.h>
#include <lvgl/src/draw/eve/lv_draw_eve_private.h>
#include <lvgl/src/draw/eve/lv_eve.h>
#include <lvgl/src/drivers/draw/eve/lv_draw_eve_display.h>
#include <lvgl/src/drivers/draw/eve/lv_draw_eve_display_defines.h>
#include <lvgl/src/libs/FT800-FT813/EVE_commands.h>

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

/* REG_ID liest 0x7c, sobald der Chip aus dem Power-Down aufgeweckt ist */
#define FT813_REG_ID_MAGIC 0x7cu

static struct canboss_eve_ctx eve_ctx;
static lv_display_t *eve_disp;

/* RAM_DL des FT81x fasst 8 KiB. Laeuft eine Displayliste ueber, faultet
 * der Koprozessor; EVE_busy() setzt ihn dann per CPURESET zurueck - beim
 * FT813 (EVE_GEN 2) OHNE den Patch-Pointer wiederherzustellen, anders als
 * bei EVE_GEN > 2. Der GT911-Patch ist danach weg und der Touch tot. */
#define FT813_RAM_DL_SIZE 8192u

static uint32_t eve_dl_last;
static uint32_t eve_dl_max;
static uint32_t eve_fault_count;

/* Haenger-Diagnose: SPI-Aktivitaet und "steckt in lv_timer_handler seit".
 * Nur der UI-Thread schreibt; racy Reads aus der Shell sind ok. */
static volatile uint32_t eve_spi_ops;
static volatile int64_t eve_lvgl_since;

/* Der SPI-Bus wird von der LVGL-Schleife (Main) und von den eve-Shell-
 * Kommandos genutzt. DRAW_EVE haelt CS ueber ganze Transaktionen hinweg,
 * darum darf sich niemand dazwischendraengen. */
static K_MUTEX_DEFINE(eve_bus_lock);

void
canboss_eve_lock(void)
{
	(void)k_mutex_lock(&eve_bus_lock, K_FOREVER);
}

void
canboss_eve_unlock(void)
{
	(void)k_mutex_unlock(&eve_bus_lock);
}

/* Von EVE_commands.c (weak Hook) waehrend FIFO-Wartezeiten aufgerufen.
 * Burst ist geschlossen - Shell kann 'eve status' dazwischen bekommen. */
void
EVE_cmdb_wait_yield(void)
{
	canboss_eve_unlock();
	k_msleep(1);
	canboss_eve_lock();
}

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

/* op_cb laeuft pro Frame tausendfach: einen defekten Bus nur begrenzt
 * melden, sonst erstickt die Konsole im Fehlerstrom. */
static void
eve_spi_err_report(const char *what, int err)
{
	static uint32_t seen;

	if (++seen <= 5u) {
		LOG_ERR("SPI %s fehlgeschlagen (%d)", what, err);
	} else if (seen == 6u) {
		LOG_ERR("weitere SPI-Fehler werden unterdrueckt");
	}
}

/* Touch-Lesen fuer den LVGL-Pointer-Indev. Ersetzt lv_draw_eve_touch_create():
 * dieses gen4-Panel meldet REG_TOUCH_SCREEN_XY transponiert zum FT81x-Default,
 * X liegt im Low-, Y im High-Halfword. LVGLs eingebauter Read-Callback wuerde
 * X/Y vertauschen und jede Beruehrung mit (echtem) X > 479 als losgelassen
 * werten. Nicht beruehrt = 0x8000-Sentinel in beiden Halbworten; ein
 * gehaltener Finger liefert den Sentinel kurz auch mal nur in EINEM Feld -
 * dann gedrueckt bleiben und die letzte gueltige Koordinate halten, sonst
 * flackert der Zustand mitten in der Beruehrung. */
static void
eve_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
	static int32_t last_x, last_y;

	ARG_UNUSED(indev);

	uint32_t xy = lv_draw_eve_memread32(eve_disp, LV_EVE_REG_TOUCH_SCREEN_XY);
	int16_t x = (int16_t)(xy & 0xffffu);
	int16_t y = (int16_t)(xy >> 16);

	if (x == INT16_MIN && y == INT16_MIN) {
		data->state = LV_INDEV_STATE_RELEASED;
		data->point.x = last_x;
		data->point.y = last_y;
		return;
	}

	if (x != INT16_MIN) {
		last_x = CLAMP(x, 0, lv_display_get_horizontal_resolution(eve_disp) - 1);
	}
	if (y != INT16_MIN) {
		last_y = CLAMP(y, 0, lv_display_get_vertical_resolution(eve_disp) - 1);
	}
	data->state = LV_INDEV_STATE_PRESSED;
	data->point.x = last_x;
	data->point.y = last_y;
}

/* Workaround fuer einen Upstream-Bug in lv_eve.c (LVGL 9.5): dessen
 * Zustands-Cache (Scissor, Farbe, Primitive, Bitmap-Register, ...) lebt in
 * static-Variablen und wird NIE pro Frame zurueckgesetzt - jede neue
 * Displayliste startet in der Hardware aber mit Default-Zustand. Stimmt der
 * veraltete Cache zufaellig mit dem angeforderten Wert ueberein, laesst
 * lv_eve das noetige DL-Kommando aus: falsche Farben, fehlende Rechtecke,
 * Glyphen aus falscher Bitmap-Quelle - die Scroll-Artefakte, denn beim
 * Scrollen aendert sich die Task-Reihenfolge von Frame zu Frame.
 *
 * Von aussen laesst sich der Cache nicht schreiben, aber erzwingen: pro
 * Eintrag ZWEI verschiedene Werte setzen, der zweite ist der DL-Default.
 * Der zweite Aufruf emittiert garantiert (Cache kann nicht beiden Werten
 * gleichen) - danach sind Cache und Hardware konsistent. Kostet ~22 Worte
 * pro Frame, verschwindend gegen die 8-KiB-RAM_DL.
 *
 * Laeuft als RENDER_START-Callback NACH dem der DRAW_EVE-Displays (Reihen-
 * folge = Registrierungsreihenfolge), also innerhalb des CMD-Bursts. */
static void
eve_state_sync_cb(lv_event_t *e)
{
	ARG_UNUSED(e);

	lv_eve_scissor(1, 1, 2, 2);
	lv_eve_scissor(0, 0, DT_PROP(FT813_NODE, width) - 1, DT_PROP(FT813_NODE, height) - 1);

	lv_eve_primitive(LV_EVE_PRIMITIVE_POINTS);
	lv_eve_primitive(LV_EVE_PRIMITIVE_LINES);

	lv_eve_color(lv_color_make(0, 0, 0));
	lv_eve_color(lv_color_make(255, 255, 255)); /* DL-Default: weiss */

	lv_eve_color_opa(254);
	lv_eve_color_opa(255);

	lv_eve_line_width(8);
	lv_eve_line_width(16); /* DL-Default: 16/16 px */

	lv_eve_point_size(2);
	lv_eve_point_size(1);

	lv_eve_color_mask(0, 0, 0, 1);
	lv_eve_color_mask(1, 1, 1, 1);

	lv_eve_stencil_func(EVE_NEVER, 0, 255);
	lv_eve_stencil_func(EVE_ALWAYS, 0, 255);

	lv_eve_stencil_op(EVE_REPLACE, EVE_REPLACE);
	lv_eve_stencil_op(EVE_KEEP, EVE_KEEP);

	lv_eve_blend_func(EVE_ONE, EVE_ONE);
	lv_eve_blend_func(EVE_SRC_ALPHA, EVE_ONE_MINUS_SRC_ALPHA);

	lv_eve_bitmap_source(4);
	lv_eve_bitmap_source(0);

	lv_eve_bitmap_size(EVE_NEAREST, EVE_BORDER, EVE_BORDER, 1, 1);
	lv_eve_bitmap_size(EVE_NEAREST, EVE_BORDER, EVE_BORDER, 0, 0);

	lv_eve_bitmap_layout(0, 1, 1);
	lv_eve_bitmap_layout(0, 0, 0);
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
			eve_spi_ops++;
			int ret = eve_spi_send(data, length);
			if (ret != 0) {
				eve_spi_err_report("send", ret);
			}
		}
		break;
	case LV_DRAW_EVE_OPERATION_SPI_RECEIVE:
		if (data != NULL && length > 0) {
			eve_spi_ops++;
			int ret = eve_spi_recv(data, length);
			if (ret != 0) {
				eve_spi_err_report("recv", ret);
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
		/* gen4-FT813-70CTP-CLB: Touch laeuft ueber die NATIVE
		 * kapazitive Engine des FT813 (REG_TOUCH_CONFIG-Default
		 * 0x8381), wie im lauffaehigen embassy-Port. has_gt911
		 * wuerde den FTDI-AN_336-Patch laden, REG_TOUCH_CONFIG auf
		 * 0x05D0 (Goodix, I2C 0x5D) umstellen und REG_GPIOX_DIR
		 * blind ueberschreiben - auf dem gen4 haengt die Reset-/
		 * INT-Leitung des Touch-Controllers an GPIO0..3, der Touch
		 * waere danach dauerhaft tot. */
		.has_gt911 = false,
		.backlight_pwm = DT_PROP(FT813_NODE, backlight_duty),
		/* 4 kHz wie der embassy-Port: 250 Hz moduliert den Backlight-
		 * Boost niederfrequent - auf schwachem GND stoert genau diese
		 * Stromwelligkeit die kapazitive Touch-Abtastung. */
		.backlight_freq = 4000,
	};

	eve_disp = lv_draw_eve_display_create(&params, eve_op_cb, &eve_ctx);
	if (eve_disp == NULL) {
		LOG_ERR("lv_draw_eve_display_create fehlgeschlagen");
		return -EIO;
	}
	eve_ctx.disp = eve_disp;

	/* NACH lv_draw_eve_display_create registrieren: der Callback muss
	 * hinter dem render_start_cb des DRAW_EVE-Displays laufen (Burst
	 * aktiv, DLSTART gesendet). */
	lv_display_add_event_cb(eve_disp, eve_state_sync_cb, LV_EVENT_RENDER_START, NULL);

	/* lv_draw_eve_display_create() verwirft den Rueckgabewert von
	 * EVE_init(); ohne eigenen Check meldet die Init auch bei totem
	 * Chip Erfolg. REG_ID zuerst beim Bring-up-Takt pruefen, dann beim
	 * Betriebstakt: antwortet nur der langsame, ist es Signalqualitaet
	 * (Jumper-Kabel), antwortet keiner, ist es Versorgung/Verdrahtung. */
	uint8_t id_slow = lv_draw_eve_memread8(eve_disp, LV_EVE_REG_ID);

	eve_spi_set_hz(eve_ctx.bus.config.frequency);
	eve_ctx.fast = true;
	uint8_t id_fast = lv_draw_eve_memread8(eve_disp, LV_EVE_REG_ID);

	if (id_slow != FT813_REG_ID_MAGIC && id_fast != FT813_REG_ID_MAGIC) {
		eve_spi_set_hz(FT813_SLOW_HZ);
		eve_ctx.fast = false;
		printk("FT813: keine Antwort (REG_ID=0x%02x bei %u Hz, 0x%02x bei %u Hz, "
		       "erwartet 0x%02x)\n",
		       id_slow, FT813_SLOW_HZ, id_fast, eve_ctx.bus.config.frequency,
		       FT813_REG_ID_MAGIC);
		printk("FT813: 5V/GND und SPI-Verdrahtung pruefen - das 7\"-Panel zieht "
		       "typ. 835 mA @ 5V\n");
		return -EIO;
	}

	if (id_fast != FT813_REG_ID_MAGIC) {
		eve_spi_set_hz(FT813_SLOW_HZ);
		eve_ctx.fast = false;
		printk("FT813: %u Hz unzuverlaessig (REG_ID=0x%02x), bleibe bei %u Hz\n",
		       eve_ctx.bus.config.frequency, id_fast, FT813_SLOW_HZ);
	}

	/* Kapazitive Engine: Kompatibilitaetsmodus (1 Punkt) + Continuous -
	 * dieselbe Konfiguration wie der lauffaehige embassy-Port. */
	lv_draw_eve_memwrite8(eve_disp, LV_EVE_REG_CTOUCH_EXTENDED, 1);
	lv_draw_eve_memwrite8(eve_disp, LV_EVE_REG_TOUCH_MODE, 3);

	/* Eigener Pointer-Indev statt lv_draw_eve_touch_create()
	 * (Achsen-Transposition, siehe eve_touch_read_cb) */
	lv_indev_t *touch_indev = lv_indev_create();

	if (touch_indev != NULL) {
		lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
		lv_indev_set_display(touch_indev, eve_disp);
		lv_indev_set_read_cb(touch_indev, eve_touch_read_cb);
	}
	lv_display_set_default(eve_disp);

	/* Das Zephyr-LVGL-Modul legt fuer zephyr,display zwangsweise ein
	 * zweites (Dummy-)Display an. Fuer DRAW_EVE ist das fatal:
	 * eve_evaluate() reisst JEDEN Draw-Task an sich, unabhaengig vom
	 * Display. Der leere Dummy-Screen schreibt seine Clear-Kommandos
	 * damit in die EVE-Displayliste und ueberdeckt die UI schwarz.
	 * Also bleibt nur das EVE-Display uebrig - der Zephyr-Dummy-Device
	 * selbst bleibt bestehen, nur LVGL rendert nicht mehr darauf. */
	lv_display_t *other = lv_display_get_next(NULL);

	while (other != NULL) {
		lv_display_t *next = lv_display_get_next(other);

		if (other != eve_disp) {
			lv_display_delete(other);
		}
		other = next;
	}

	printk("FT813 DRAW_EVE bereit: %ux%u, SPI %u Hz, touch-config=0x%04x "
	       "(Diagnose: 'eve status', 'eve touch')\n",
	       params.hor_res, params.ver_res, eve_ctx.spi_cfg.frequency,
	       lv_draw_eve_memread16(eve_disp, LV_EVE_REG_TOUCH_CONFIG));
	return 0;
}

lv_display_t *
canboss_eve_display_get(void)
{
	return eve_disp;
}

uint32_t
canboss_eve_timer_handler(void)
{
	int64_t t0;
	uint32_t sleep_ms;
	int64_t elapsed;

	canboss_eve_lock();
	t0 = k_uptime_get();

	/* Safety-Netz: Upstream-flush_cb (und aeltere Binaries) liessen den
	 * CMD-Burst zwischen Frames offen. Touch-Reads in lv_timer_handler
	 * duerfen nicht in eine offene CMDB-Transaktion greifen. */
	EVE_end_cmd_burst();

	eve_lvgl_since = t0;
	sleep_ms = lv_timer_handler();
	eve_lvgl_since = 0;

	/* Auch nach dem Frame: Burst zu, bevor Diagnose-Register gelesen
	 * werden (und bevor der naechste Indev-Read kommt). */
	EVE_end_cmd_burst();

	/* REG_CMD_DL haelt die Groesse der letzten Displayliste, bis das
	 * naechste CMD_DLSTART sie auf 0 setzt - also die Fuellhoehe des
	 * gerade gebauten Frames. */
	eve_dl_last = lv_draw_eve_memread32(eve_disp, LV_EVE_REG_CMD_DL);
	/* Nach Fault/SPI-Muell kann REG_CMD_DL 0xffffffff lesen - nicht
	 * als Max uebernehmen (Status zeigte sonst 524287%%). */
	if (eve_dl_last <= FT813_RAM_DL_SIZE && eve_dl_last > eve_dl_max) {
		eve_dl_max = eve_dl_last;
		/* Fruehwarnung auf der UART, bevor der Koprozessor faultet -
		 * PoC/dichte Screens „haengen“ sonst ohne LVGL-OOM-Meldung. */
		if (eve_dl_max >= (FT813_RAM_DL_SIZE * 7u) / 8u) {
			printk("EVE: Displayliste %u/%u Bytes (>%u%%) - Screen zu komplex\n",
			       eve_dl_max, FT813_RAM_DL_SIZE,
			       (eve_dl_max * 100u) / FT813_RAM_DL_SIZE);
		}
	}

	if (EVE_get_and_reset_fault_state() == EVE_FAULT_RECOVERED) {
		eve_fault_count++;

		/* CPURESET hat die Touch-Register mitgenommen. */
		lv_draw_eve_memwrite8(eve_disp, LV_EVE_REG_CTOUCH_EXTENDED, 1);
		lv_draw_eve_memwrite8(eve_disp, LV_EVE_REG_TOUCH_MODE, 3);

		if (eve_fault_count <= 5u) {
			printk("EVE: Koprozessor-Fault #%u, Displayliste %u/%u Bytes - "
			       "Screen vereinfachen (Border/Radius/Widgets)\n",
			       eve_fault_count, eve_dl_last, FT813_RAM_DL_SIZE);
		}
	}

	elapsed = k_uptime_get() - t0;
	if (elapsed >= 200) {
		printk("EVE: lv_timer_handler dauerte %lld ms (DL %u/%u, faults %u)\n",
		       elapsed, eve_dl_last, FT813_RAM_DL_SIZE, eve_fault_count);
	}

	canboss_eve_unlock();
	return sleep_ms;
}

#ifdef CONFIG_SHELL

static int
eve_shell_ready(const struct shell *sh)
{
	if (eve_disp == NULL) {
		shell_error(sh, "EVE-Display nicht initialisiert");
		return -ENODEV;
	}
	return 0;
}

/* Shell darf den UI-Thread nicht ewig blockieren: haengt Main in
 * lv_timer_handler/EVE_execute_cmd, bleibt der Mutex sonst fuer immer weg
 * und 'eve status' wirkt tot. */
static int
eve_shell_lock(const struct shell *sh)
{
	if (k_mutex_lock(&eve_bus_lock, K_MSEC(800)) != 0) {
		/* Wer haengt wie? SPI-Aktivitaet 100 ms lang beobachten:
		 * Ops > 0  -> Renderschleife kommt nicht zum Ende
		 * Ops == 0 -> reiner CPU-Spin (LVGL-Assert, Dispatch-Wait, ...) */
		uint32_t ops0 = eve_spi_ops;
		int64_t since = eve_lvgl_since;

		k_msleep(100);

		uint32_t delta = eve_spi_ops - ops0;

		if (since != 0) {
			shell_error(sh, "EVE-Bus belegt - UI-Thread steckt seit %lld ms in "
					"lv_timer_handler",
				    k_uptime_get() - since);
		} else {
			shell_error(sh, "EVE-Bus belegt (>800 ms), aber nicht in lv_timer_handler");
		}
		if (delta > 0) {
			shell_error(sh, "SPI aktiv (%u Ops/100 ms): Frame wird endlos neu "
					"gebaut - Displayliste/Timer pruefen ('eve status' spaeter "
					"erneut)",
				    delta);
		} else {
			shell_error(sh, "kein SPI-Verkehr: CPU-Spin - LVGL-Assert (Heap voll? "
					"CONFIG_LV_Z_MEM_POOL_SIZE), Dispatch-Wait oder "
					"Endlosschleife; Konsole auf LVGL-ASSERT/-WARN pruefen");
		}
		return -EBUSY;
	}
	return 0;
}

static int
cmd_eve_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (eve_shell_ready(sh) != 0) {
		return -ENODEV;
	}

	if (eve_shell_lock(sh) != 0) {
		return -EBUSY;
	}
	uint8_t id = lv_draw_eve_memread8(eve_disp, LV_EVE_REG_ID);
	uint32_t clock0 = lv_draw_eve_memread32(eve_disp, LV_EVE_REG_CLOCK);
	uint32_t frames0 = lv_draw_eve_memread32(eve_disp, LV_EVE_REG_FRAMES);
	uint16_t space = lv_draw_eve_memread16(eve_disp, LV_EVE_REG_CMDB_SPACE);
	uint8_t pwm = lv_draw_eve_memread8(eve_disp, LV_EVE_REG_PWM_DUTY);
	uint8_t cpureset = lv_draw_eve_memread8(eve_disp, LV_EVE_REG_CPURESET);
	uint16_t tcfg = lv_draw_eve_memread16(eve_disp, LV_EVE_REG_TOUCH_CONFIG);
	uint8_t ext = lv_draw_eve_memread8(eve_disp, LV_EVE_REG_CTOUCH_EXTENDED);
	uint8_t tmode = lv_draw_eve_memread8(eve_disp, LV_EVE_REG_TOUCH_MODE);
	canboss_eve_unlock();

	k_msleep(100);

	if (eve_shell_lock(sh) != 0) {
		return -EBUSY;
	}
	uint32_t clock1 = lv_draw_eve_memread32(eve_disp, LV_EVE_REG_CLOCK);
	uint32_t frames1 = lv_draw_eve_memread32(eve_disp, LV_EVE_REG_FRAMES);
	canboss_eve_unlock();

	shell_print(sh, "SPI          : %u Hz (%s)", eve_ctx.spi_cfg.frequency,
		    eve_ctx.fast ? "Betrieb" : "Bring-up");
	shell_print(sh, "REG_ID       : 0x%02x   %s", id,
		    id == FT813_REG_ID_MAGIC ? "ok" : "ERWARTET 0x7c -> Chip antwortet nicht");
	shell_print(sh, "REG_CLOCK    : %u -> %u  %s", clock0, clock1,
		    clock1 != clock0 ? "laeuft" : "STEHT -> Chip ohne Takt/Strom");
	shell_print(sh, "REG_FRAMES   : %u -> %u  %s", frames0, frames1,
		    frames1 != frames0 ? "laeuft" : "STEHT -> kein Pixeltakt (PCLK/DISP)");
	shell_print(sh, "REG_CMDB_SPACE: 0x%03x %s", space,
		    (space & 3u) != 0u ? "Koprozessor-Fehler!" : (space == 0xffcu ? "idle" : "busy"));
	shell_print(sh, "REG_PWM_DUTY : %u (Backlight)", pwm);
	shell_print(sh, "REG_CPURESET : %u %s", cpureset, cpureset == 0u ? "(laeuft)" : "(im Reset!)");
	/* Bit 15 waehlt die Touch-Engine: 0 = kapazitiv (FT811/813-Default
	 * 0x0381), 1 = resistiv (FT810/812-Default 0x8381). */
	shell_print(sh, "TOUCH_CONFIG : 0x%04x %s", tcfg,
		    tcfg == 0x0381u ? "kapazitiv nativ (gen4 ok)"
				    : (tcfg == 0x05d0u ? "GT911-Patch - FALSCH fuer gen4!"
						       : "unerwarteter Wert"));
	shell_print(sh, "CTOUCH_EXT   : %u %s", ext, ext == 1u ? "(compat/1 Punkt)" : "(extended)");
	shell_print(sh, "TOUCH_MODE   : %u %s", tmode, tmode == 3u ? "(continuous)" : "");

	shell_print(sh, "Displayliste : %u Bytes, max %u von %u (%u%%)", eve_dl_last, eve_dl_max,
		    FT813_RAM_DL_SIZE, (eve_dl_max * 100u) / FT813_RAM_DL_SIZE);
	if (eve_dl_max >= FT813_RAM_DL_SIZE - 512u) {
		shell_print(sh, "               RAM_DL fast voll -> Screen vereinfachen");
	}
	shell_print(sh, "Koprozessor  : %u Faults%s", eve_fault_count,
		    eve_fault_count != 0u ? " -> Displaylisten-Ueberlauf, Touch nach Fault tot" : "");
	shell_print(sh, "RAM_G        : %u KiB von 1024 belegt, %u Eintraege (wird nie freigegeben)",
		    lv_draw_eve_unit_g->ramg.ramg_addr_end / 1024u,
		    lv_draw_eve_unit_g->ramg.hash_table_cells_occupied);
	return 0;
}

/* Nullt die Zaehler, damit sich Fuellhoehe und Faults einem einzelnen
 * Screen zuordnen lassen: reset, hinnavigieren, status. */
static int
cmd_eve_reset(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	eve_dl_max = 0;
	eve_fault_count = 0;
	shell_print(sh, "Zaehler genullt - jetzt zum fraglichen Screen navigieren, dann 'eve status'");
	return 0;
}

static int
cmd_eve_touch(const struct shell *sh, size_t argc, char **argv)
{
	if (eve_shell_ready(sh) != 0) {
		return -ENODEV;
	}

	uint32_t secs = (argc > 1) ? (uint32_t)strtoul(argv[1], NULL, 10) : 10u;

	if (secs == 0u || secs > 120u) {
		shell_error(sh, "Dauer 1..120 s");
		return -EINVAL;
	}

	shell_print(sh, "Panel %u s antippen - Aenderungen an XY/TAG werden gemeldet ...", secs);

	uint32_t last_xy = 0xffffffffu;
	uint32_t events = 0;
	int64_t deadline = k_uptime_get() + (int64_t)secs * 1000;

	while (k_uptime_get() < deadline) {
		if (eve_shell_lock(sh) != 0) {
			return -EBUSY;
		}
		uint32_t xy = lv_draw_eve_memread32(eve_disp, LV_EVE_REG_TOUCH_SCREEN_XY);
		uint32_t raw = lv_draw_eve_memread32(eve_disp, LV_EVE_REG_TOUCH_RAW_XY);
		uint8_t tag = lv_draw_eve_memread8(eve_disp, LV_EVE_REG_TOUCH_TAG);
		canboss_eve_unlock();

		if (xy != last_xy) {
			/* gen4: Achsen transponiert - X im Low-, Y im High-Halfword */
			int16_t x = (int16_t)(xy & 0xffffu);
			int16_t y = (int16_t)(xy >> 16);

			if (xy == 0x80008000u) {
				shell_print(sh, "  losgelassen");
			} else {
				shell_print(sh, "  x=%d y=%d tag=%u raw=0x%08x", x, y, tag, raw);
			}
			last_xy = xy;
			events++;
		}
		k_msleep(30);
	}

	if (events == 0u) {
		shell_print(sh, "keine Aenderung - REG_TOUCH_SCREEN_XY blieb 0x%08x", last_xy);
		shell_print(sh, "0xffffffff => SPI liest nichts; 0x80008000 => Chip ok, "
				"aber kein Touch erkannt ('eve status' pruefen)");
	} else {
		shell_print(sh, "%u Aenderungen - Touch liefert Daten", events);
	}
	return 0;
}

static int
cmd_eve_reg(const struct shell *sh, size_t argc, char **argv)
{
	if (eve_shell_ready(sh) != 0) {
		return -ENODEV;
	}

	uint32_t addr = (uint32_t)strtoul(argv[1], NULL, 16);
	uint32_t width = (argc > 2) ? (uint32_t)strtoul(argv[2], NULL, 10) : 8u;
	uint32_t val;

	if (eve_shell_lock(sh) != 0) {
		return -EBUSY;
	}
	switch (width) {
	case 8:
		val = lv_draw_eve_memread8(eve_disp, addr);
		break;
	case 16:
		val = lv_draw_eve_memread16(eve_disp, addr);
		break;
	case 32:
		val = lv_draw_eve_memread32(eve_disp, addr);
		break;
	default:
		canboss_eve_unlock();
		shell_error(sh, "Breite muss 8, 16 oder 32 sein");
		return -EINVAL;
	}
	canboss_eve_unlock();

	shell_print(sh, "[0x%06x] = 0x%0*x (%u)", addr, (int)(width / 4), val, val);
	return 0;
}

static int
cmd_eve_pwm(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	if (eve_shell_ready(sh) != 0) {
		return -ENODEV;
	}

	uint32_t duty = (uint32_t)strtoul(argv[1], NULL, 10);

	if (duty > 128u) {
		shell_error(sh, "Tastgrad 0..128");
		return -EINVAL;
	}

	if (eve_shell_lock(sh) != 0) {
		return -EBUSY;
	}
	lv_draw_eve_memwrite8(eve_disp, LV_EVE_REG_PWM_DUTY, (uint8_t)duty);
	canboss_eve_unlock();

	shell_print(sh, "Backlight auf %u gesetzt - sichtbare Aenderung beweist "
			"funktionierende SPI-Schreibrichtung",
		    duty);
	return 0;
}

/* Zeigt, auf welchem Display die UI liegt: das Zephyr-LVGL-Modul bindet
 * zwingend das Dummy-Chosen, das EVE-Display kommt erst danach dazu. */
static int
cmd_eve_lvgl(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (eve_shell_lock(sh) != 0) {
		return -EBUSY;
	}
	lv_display_t *def = lv_display_get_default();
	unsigned int idx = 0;

	for (lv_display_t *d = lv_display_get_next(NULL); d != NULL; d = lv_display_get_next(d)) {
		lv_obj_t *scr = lv_display_get_screen_active(d);

		shell_print(sh, "Display %u: %dx%d%s%s, Screen %p, %u Kinder", idx,
			    lv_display_get_horizontal_resolution(d),
			    lv_display_get_vertical_resolution(d), d == def ? " [default]" : "",
			    d == eve_disp ? " [EVE]" : " [dummy]", (void *)scr,
			    scr != NULL ? lv_obj_get_child_count(scr) : 0u);
		shell_print(sh, "  inv_p=%u render_mode=%u rendering=%u refr_timer=%s", d->inv_p,
			    (unsigned int)d->render_mode, (unsigned int)d->rendering_in_progress,
			    d->refr_timer != NULL ? "ja" : "NEIN");
		idx++;
	}
	canboss_eve_unlock();
	return 0;
}

/* Erzwingt einen Neuaufbau und prueft, ob die Displayliste im Chip
 * dadurch tatsaechlich waechst. */
static int
cmd_eve_redraw(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (eve_shell_ready(sh) != 0) {
		return -ENODEV;
	}

	if (eve_shell_lock(sh) != 0) {
		return -EBUSY;
	}
	uint32_t wr0 = lv_draw_eve_memread32(eve_disp, LV_EVE_REG_CMD_WRITE);
	lv_obj_t *scr = lv_display_get_screen_active(eve_disp);

	if (scr != NULL) {
		lv_obj_invalidate(scr);
	}
	canboss_eve_unlock();

	/* die Main-Schleife rendern lassen */
	k_msleep(300);

	if (eve_shell_lock(sh) != 0) {
		return -EBUSY;
	}
	uint32_t wr1 = lv_draw_eve_memread32(eve_disp, LV_EVE_REG_CMD_WRITE);
	canboss_eve_unlock();

	/* RAM_CMD ist 4 KiB, der Schreibzeiger laeuft also um. RAM_DL taugt
	 * hier nicht als Indikator: DLSWAP wechselt zwischen zwei Listen,
	 * ein Lesen von 0x300000 trifft mal die alte, mal die neue. */
	uint32_t written = (wr1 - wr0) & 0xfffu;

	shell_print(sh, "REG_CMD_WRITE : %u -> %u (%u Bytes Displayliste)", wr0, wr1, written);

	if (written == 0u) {
		shell_print(sh, "kein Kommando abgesetzt -> LVGL rendert nicht auf das EVE-Display");
	} else {
		shell_print(sh, "Renderpfad ok - die UI wird in den Chip geschrieben");
	}
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	eve_cmds,
	SHELL_CMD(lvgl, NULL, "LVGL-Displays und Screens auflisten", cmd_eve_lvgl),
	SHELL_CMD(redraw, NULL, "Neuaufbau erzwingen und Displayliste pruefen", cmd_eve_redraw),
	SHELL_CMD(status, NULL, "FT813-Zustand (ID, Takt, Frames, Touch-Register)", cmd_eve_status),
	SHELL_CMD(reset, NULL, "Displaylisten-/Fault-Zaehler nullen", cmd_eve_reset),
	SHELL_CMD_ARG(touch, NULL, "Touch-Register live mitlesen: eve touch [Sekunden]",
		      cmd_eve_touch, 1, 1),
	SHELL_CMD_ARG(reg, NULL, "Register lesen: eve reg <hex-Adresse> [8|16|32]", cmd_eve_reg, 2, 1),
	SHELL_CMD_ARG(pwm, NULL, "Backlight setzen: eve pwm <0..128>", cmd_eve_pwm, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(eve, &eve_cmds, "FT813/EVE-Diagnose", NULL);

#endif /* CONFIG_SHELL */
