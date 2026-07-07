/**
 ******************************************************************************
 * @file    ft81x.c
 * @brief   Minimaler FT81x-(EVE2-)Hosttreiber ueber blockierendes SPI.
 *
 * C-Port des Rust-Treibers examples/gen4-ft813-70ctp-h573i-dk/src/ft81x.rs
 * aus dem embassy-Repo (gleiche Registerfolge, gleiche Panel-Timings) —
 * Aenderungen dort nachziehen. Referenzen: FT81x-Datenblatt + BRT_AN_033;
 * Timings = Standard-800x480-WVGA-Satz der gen4-FT812/FT813-70-Module,
 * kein externer Quarz -> CLKINT mit automatischem CLKEXT-Fallback.
 *
 * Single-Owner: LVGL-Flush und Touch-Poll laufen beide im LVGL-Task
 * (lv_timer_handler), es gibt keine nebenlaeufigen SPI-Zugriffe.
 ******************************************************************************
 */

#include "ft81x.h"
#include "main.h"
#include "spi.h"

/* ── FT81x-Speicherkarte ──────────────────────────────────────────────────── */
#define RAM_G                 0x000000UL /* 1 MiB Grafik-RAM, Framebuffer bei 0 */
#define RAM_DL                0x300000UL

#define REG_ID                0x302000UL
#define REG_CPURESET          0x302020UL
#define REG_HCYCLE            0x30202CUL
#define REG_HOFFSET           0x302030UL
#define REG_HSIZE             0x302034UL
#define REG_HSYNC0            0x302038UL
#define REG_HSYNC1            0x30203CUL
#define REG_VCYCLE            0x302040UL
#define REG_VOFFSET           0x302044UL
#define REG_VSIZE             0x302048UL
#define REG_VSYNC0            0x30204CUL
#define REG_VSYNC1            0x302050UL
#define REG_DLSWAP            0x302054UL
#define REG_DITHER            0x302060UL
#define REG_SWIZZLE           0x302064UL
#define REG_CSPREAD           0x302068UL
#define REG_PCLK_POL          0x30206CUL
#define REG_PCLK              0x302070UL
#define REG_GPIOX_DIR         0x302098UL
#define REG_GPIOX             0x30209CUL
#define REG_PWM_HZ            0x3020D0UL
#define REG_PWM_DUTY          0x3020D4UL
#define REG_TOUCH_MODE        0x302104UL
#define REG_CTOUCH_EXTENDED   0x302108UL
#define REG_TOUCH_SCREEN_XY   0x302124UL

/* ── Host-Kommandos (3-Byte-Rahmen) ───────────────────────────────────────── */
#define HCMD_ACTIVE           0x00
#define HCMD_CLKEXT           0x44
#define HCMD_CLKINT           0x48
#define HCMD_RST_PULSE        0x68

/* ── Panel-Timings: 4D Systems gen4-FT813-70 (Standard-WVGA-Satz) ─────────── */
#define H_CYCLE               928
#define H_OFFSET              88
#define H_SYNC0               0
#define H_SYNC1               48
#define V_CYCLE               525
#define V_OFFSET              32
#define V_SYNC0               0
#define V_SYNC1               3
#define PCLK_DIV              2 /* 60 MHz / 2 = 30 MHz Pixeltakt */
#define PCLK_POL              1 /* fallende Flanke */
#define SWIZZLE               0
#define CSPREAD               0

/* ── Displaylisten-Kodierung (Teilmenge) ──────────────────────────────────── */
#define DL_CLEAR_COLOR_RGB(r, g, b) (0x02000000UL | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define DL_CLEAR()                  0x26000007UL /* Farbe + Stencil + Tag */
#define DL_BITMAP_HANDLE(h)         (0x05000000UL | ((uint32_t)(h) & 0x1FUL))
#define DL_BITMAP_SOURCE(addr)      (0x01000000UL | ((uint32_t)(addr) & 0x3FFFFFUL))
/* BITMAP_LAYOUT — Format 7 = RGB565; untere 10/9 Bit von Stride/Hoehe */
#define DL_BITMAP_LAYOUT(fmt, stride, h) \
	(0x07000000UL | ((uint32_t)(fmt) << 19) | (((uint32_t)(stride) & 0x3FFUL) << 9) | ((uint32_t)(h) & 0x1FFUL))
#define DL_BITMAP_LAYOUT_H(stride, h) \
	(0x28000000UL | ((((uint32_t)(stride) >> 10) & 0x3UL) << 2) | (((uint32_t)(h) >> 9) & 0x3UL))
/* BITMAP_SIZE — NEAREST-Filter, BORDER-Wrap; untere 9 Bit von w/h */
#define DL_BITMAP_SIZE(w, h) \
	(0x08000000UL | (((uint32_t)(w) & 0x1FFUL) << 9) | ((uint32_t)(h) & 0x1FFUL))
#define DL_BITMAP_SIZE_H(w, h) \
	(0x29000000UL | ((((uint32_t)(w) >> 9) & 0x3UL) << 2) | (((uint32_t)(h) >> 9) & 0x3UL))
#define DL_BEGIN_BITMAPS()          0x1F000001UL
#define DL_VERTEX2II(x, y) \
	(0x80000000UL | (((uint32_t)(x) & 0x1FFUL) << 21) | (((uint32_t)(y) & 0x1FFUL) << 12))
#define DL_END()                    0x21000000UL
#define DL_DISPLAY()                0x00000000UL
#define DLSWAP_FRAME                2UL

#define SPI_TIMEOUT_MS        100U

/* ── SPI-Grundoperationen (Software-/CS auf PA3) ──────────────────────────── */

static inline void cs_low(void)  { HAL_GPIO_WritePin(FT813_CS_GPIO_Port, FT813_CS_Pin, GPIO_PIN_RESET); }
static inline void cs_high(void) { HAL_GPIO_WritePin(FT813_CS_GPIO_Port, FT813_CS_Pin, GPIO_PIN_SET); }

static ft81x_status_t host_command(uint8_t cmd)
{
	uint8_t frame[3] = { cmd, 0x00, 0x00 };
	HAL_StatusTypeDef st;

	cs_low();
	st = HAL_SPI_Transmit(&hspi2, frame, sizeof(frame), SPI_TIMEOUT_MS);
	cs_high();
	return (st == HAL_OK) ? FT81X_OK : FT81X_ERR_SPI;
}

/* Burst-Write nach addr (auto-inkrementierend) */
static ft81x_status_t wr_bytes(uint32_t addr, const uint8_t *data, uint32_t len)
{
	uint8_t hdr[3] = {
		(uint8_t)(((addr >> 16) & 0x3F) | 0x80),
		(uint8_t)(addr >> 8),
		(uint8_t)addr,
	};
	HAL_StatusTypeDef st;

	cs_low();
	st = HAL_SPI_Transmit(&hspi2, hdr, sizeof(hdr), SPI_TIMEOUT_MS);
	if (st == HAL_OK && len > 0)
	{
		/* HAL_SPI_Transmit nimmt uint16_t-Laengen — Streifenpuffer bleiben
		 * weit darunter, grosse Flaechen laufen zeilenweise. */
		st = HAL_SPI_Transmit(&hspi2, (uint8_t *)data, (uint16_t)len, SPI_TIMEOUT_MS);
	}
	cs_high();
	return (st == HAL_OK) ? FT81X_OK : FT81X_ERR_SPI;
}

static ft81x_status_t rd_bytes(uint32_t addr, uint8_t *data, uint16_t len)
{
	/* 3 Adressbytes (Top-Bits 00 = Read) + 1 Dummy-Byte */
	uint8_t hdr[4] = {
		(uint8_t)((addr >> 16) & 0x3F),
		(uint8_t)(addr >> 8),
		(uint8_t)addr,
		0x00,
	};
	HAL_StatusTypeDef st;

	cs_low();
	st = HAL_SPI_Transmit(&hspi2, hdr, sizeof(hdr), SPI_TIMEOUT_MS);
	if (st == HAL_OK)
	{
		st = HAL_SPI_Receive(&hspi2, data, len, SPI_TIMEOUT_MS);
	}
	cs_high();
	return (st == HAL_OK) ? FT81X_OK : FT81X_ERR_SPI;
}

static ft81x_status_t wr8(uint32_t addr, uint8_t v)   { return wr_bytes(addr, &v, 1); }

static ft81x_status_t wr16(uint32_t addr, uint16_t v)
{
	uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
	return wr_bytes(addr, b, sizeof(b));
}

static ft81x_status_t wr32(uint32_t addr, uint32_t v)
{
	uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
	return wr_bytes(addr, b, sizeof(b));
}

static ft81x_status_t rd8(uint32_t addr, uint8_t *v)  { return rd_bytes(addr, v, 1); }

static ft81x_status_t rd32(uint32_t addr, uint32_t *v)
{
	uint8_t b[4];
	ft81x_status_t st = rd_bytes(addr, b, sizeof(b));
	if (st == FT81X_OK)
	{
		*v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
	}
	return st;
}

/* ── Bring-up ─────────────────────────────────────────────────────────────── */

static void power_cycle(void)
{
	HAL_GPIO_WritePin(FT813_PD_GPIO_Port, FT813_PD_Pin, GPIO_PIN_RESET);
	HAL_Delay(6);
	HAL_GPIO_WritePin(FT813_PD_GPIO_Port, FT813_PD_Pin, GPIO_PIN_SET);
	HAL_Delay(21);
	/* Sicherheitshalber zusaetzlich Core-Reset per Host-Kommando */
	(void)host_command(HCMD_RST_PULSE);
	HAL_Delay(21);
}

static bool poll_chip_id(void)
{
	uint32_t deadline = HAL_GetTick() + 400U;
	while ((int32_t)(deadline - HAL_GetTick()) > 0)
	{
		uint8_t id = 0;
		if (rd8(REG_ID, &id) == FT81X_OK && id == 0x7C)
		{
			return true;
		}
		HAL_Delay(5);
	}
	return false;
}

static ft81x_status_t configure(void)
{
	uint32_t deadline = HAL_GetTick() + 400U;
	uint8_t cpureset = 0xFF;
	uint32_t gpiox = 0;
	ft81x_status_t st;

	/* Warten bis die Grafik-Engine den Reset verlaesst */
	for (;;)
	{
		st = rd8(REG_CPURESET, &cpureset);
		if (st != FT81X_OK) return st;
		if (cpureset == 0) break;
		if ((int32_t)(deadline - HAL_GetTick()) <= 0) return FT81X_ERR_RESET_TIMEOUT;
		HAL_Delay(5);
	}

	/* Panel-Timings (REG_PCLK bleibt 0 = Display aus, bis ganz zum Schluss) */
	if ((st = wr16(REG_HSIZE, FT81X_DISPLAY_WIDTH)) != FT81X_OK) return st;
	if ((st = wr16(REG_HCYCLE, H_CYCLE)) != FT81X_OK) return st;
	if ((st = wr16(REG_HOFFSET, H_OFFSET)) != FT81X_OK) return st;
	if ((st = wr16(REG_HSYNC0, H_SYNC0)) != FT81X_OK) return st;
	if ((st = wr16(REG_HSYNC1, H_SYNC1)) != FT81X_OK) return st;
	if ((st = wr16(REG_VSIZE, FT81X_DISPLAY_HEIGHT)) != FT81X_OK) return st;
	if ((st = wr16(REG_VCYCLE, V_CYCLE)) != FT81X_OK) return st;
	if ((st = wr16(REG_VOFFSET, V_OFFSET)) != FT81X_OK) return st;
	if ((st = wr16(REG_VSYNC0, V_SYNC0)) != FT81X_OK) return st;
	if ((st = wr16(REG_VSYNC1, V_SYNC1)) != FT81X_OK) return st;
	if ((st = wr8(REG_SWIZZLE, SWIZZLE)) != FT81X_OK) return st;
	if ((st = wr8(REG_PCLK_POL, PCLK_POL)) != FT81X_OK) return st;
	if ((st = wr8(REG_CSPREAD, CSPREAD)) != FT81X_OK) return st;
	if ((st = wr8(REG_DITHER, 1)) != FT81X_OK) return st;

	/* Kapazitiver Touch: Kompatibilitaetsmodus (Single-Touch), kontinuierlich */
	if ((st = wr8(REG_CTOUCH_EXTENDED, 1)) != FT81X_OK) return st;
	if ((st = wr8(REG_TOUCH_MODE, 3)) != FT81X_OK) return st;

	/* Leere Displayliste, damit das erste sichtbare Bild schwarz ist */
	if ((st = wr32(RAM_DL, DL_CLEAR_COLOR_RGB(0, 0, 0))) != FT81X_OK) return st;
	if ((st = wr32(RAM_DL + 4, DL_CLEAR())) != FT81X_OK) return st;
	if ((st = wr32(RAM_DL + 8, DL_DISPLAY())) != FT81X_OK) return st;
	if ((st = wr32(REG_DLSWAP, DLSWAP_FRAME)) != FT81X_OK) return st;

	/* DISP-Pin high (GPIOX Bit 15), Backlight-PWM bereit aber dunkel */
	if ((st = wr16(REG_GPIOX_DIR, 0x8000)) != FT81X_OK) return st;
	if ((st = rd32(REG_GPIOX, &gpiox)) != FT81X_OK) return st;
	if ((st = wr16(REG_GPIOX, (uint16_t)gpiox | 0x8000)) != FT81X_OK) return st;
	if ((st = wr16(REG_PWM_HZ, 250)) != FT81X_OK) return st;
	if ((st = wr8(REG_PWM_DUTY, 0)) != FT81X_OK) return st;

	/* Pixeltakt starten — das Panel scannt ab jetzt */
	return wr8(REG_PCLK, PCLK_DIV);
}

ft81x_status_t ft81x_init(void)
{
	/* gen4-FT813-Module laufen vom internen Oszillator. Erst CLKINT
	 * versuchen, einmalig auf CLKEXT zurueckfallen (falls eine
	 * Modulvariante doch einen Quarz bestueckt). */
	static const uint8_t clk_cmds[2] = { HCMD_CLKINT, HCMD_CLKEXT };

	for (size_t attempt = 0; attempt < 2; attempt++)
	{
		ft81x_status_t st;

		power_cycle();
		if ((st = host_command(clk_cmds[attempt])) != FT81X_OK) return st;
		if ((st = host_command(HCMD_ACTIVE)) != FT81X_OK) return st;
		HAL_Delay(40);

		if (poll_chip_id())
		{
			return configure();
		}
	}
	return FT81X_ERR_CHIP_ID;
}

void ft81x_set_spi_run_speed(void)
{
	/* 250 MHz Kerneltakt / 16 = 15.625 MHz (FT81x-Maximum: 30 MHz) */
	spi2_set_prescaler(SPI_BAUDRATEPRESCALER_16);
}

ft81x_status_t ft81x_set_backlight(uint8_t duty)
{
	return wr8(REG_PWM_DUTY, (duty > 128) ? 128 : duty);
}

ft81x_status_t ft81x_clear_framebuffer(uint16_t rgb565)
{
	uint8_t line[FT81X_LINE_STRIDE];
	ft81x_status_t st;

	for (size_t i = 0; i < sizeof(line); i += 2)
	{
		line[i] = (uint8_t)rgb565;
		line[i + 1] = (uint8_t)(rgb565 >> 8);
	}
	for (uint32_t y = 0; y < FT81X_DISPLAY_HEIGHT; y++)
	{
		st = wr_bytes(RAM_G + y * FT81X_LINE_STRIDE, line, sizeof(line));
		if (st != FT81X_OK) return st;
	}
	return FT81X_OK;
}

ft81x_status_t ft81x_show_framebuffer(void)
{
	const uint32_t w = FT81X_DISPLAY_WIDTH;
	const uint32_t h = FT81X_DISPLAY_HEIGHT;
	const uint32_t stride = FT81X_LINE_STRIDE;
	const uint32_t dl[] = {
		DL_CLEAR_COLOR_RGB(0, 0, 0),
		DL_CLEAR(),
		DL_BITMAP_HANDLE(0),
		DL_BITMAP_SOURCE(RAM_G),
		DL_BITMAP_LAYOUT(7, stride, h), /* 7 = RGB565 */
		DL_BITMAP_LAYOUT_H(stride, h),
		DL_BITMAP_SIZE(w, h),
		DL_BITMAP_SIZE_H(w, h),
		DL_BEGIN_BITMAPS(),
		DL_VERTEX2II(0, 0),
		DL_END(),
		DL_DISPLAY(),
	};
	ft81x_status_t st;

	for (size_t i = 0; i < sizeof(dl) / sizeof(dl[0]); i++)
	{
		st = wr32(RAM_DL + 4U * i, dl[i]);
		if (st != FT81X_OK) return st;
	}
	return wr32(REG_DLSWAP, DLSWAP_FRAME);
}

ft81x_status_t ft81x_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *px)
{
	const uint32_t row_bytes = (uint32_t)w * 2U;
	ft81x_status_t st;

	if (x == 0 && w == FT81X_DISPLAY_WIDTH)
	{
		/* Volle Breite -> zusammenhaengend im RAM_G: ein einziger Burst,
		 * solange die HAL-Transferlaenge (uint16_t) reicht; sonst in
		 * Zeilenbloecken. Ein 40-Zeilen-LVGL-Streifen (64000 B) passt. */
		uint32_t total = row_bytes * h;
		uint32_t offset = 0;
		while (total > 0)
		{
			uint32_t chunk = total;
			if (chunk > 0xFFFFU)
			{
				chunk = (0xFFFFU / row_bytes) * row_bytes; /* ganze Zeilen */
			}
			st = wr_bytes(RAM_G + (uint32_t)y * FT81X_LINE_STRIDE + offset, px + offset, chunk);
			if (st != FT81X_OK) return st;
			offset += chunk;
			total -= chunk;
		}
		return FT81X_OK;
	}
	for (uint32_t row = 0; row < h; row++)
	{
		uint32_t addr = RAM_G + ((uint32_t)y + row) * FT81X_LINE_STRIDE + (uint32_t)x * 2U;
		st = wr_bytes(addr, px + row * row_bytes, row_bytes);
		if (st != FT81X_OK) return st;
	}
	return FT81X_OK;
}

ft81x_status_t ft81x_touch_read(ft81x_touch_t *point)
{
	uint32_t xy = 0;
	ft81x_status_t st = rd32(REG_TOUCH_SCREEN_XY, &xy);

	if (st != FT81X_OK) return st;

	if (xy == 0x80008000UL)
	{
		point->x = 0;
		point->y = 0;
		point->pressed = false;
		return FT81X_OK;
	}

	int32_t x = (int16_t)(xy >> 16);
	int32_t y = (int16_t)(xy & 0xFFFF);
	if (x < 0) x = 0;
	if (x > FT81X_DISPLAY_WIDTH - 1) x = FT81X_DISPLAY_WIDTH - 1;
	if (y < 0) y = 0;
	if (y > FT81X_DISPLAY_HEIGHT - 1) y = FT81X_DISPLAY_HEIGHT - 1;

	point->x = x;
	point->y = y;
	point->pressed = true;
	return FT81X_OK;
}
