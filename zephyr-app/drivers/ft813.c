/**
 * ft813.c
 *
 * Zephyr-Displaytreiber fuer den FT813 (EVE2) als "dummer Framebuffer"
 * plus Touch-Input - Port von Core/Src/ft81x.c (STM32-HAL-Version,
 * ihrerseits C-Port des embassy-Rust-Treibers; gleiche Registerfolge,
 * gleiche Panel-Timings).
 *
 *  - display_write(): RGB565-Rechtecke (LVGL-Flush) per SPI-Burst in
 *    das RAM_G; eine statische Displayliste scannt die Bitmap aufs Panel
 *  - display_blanking_off(): Backlight an (REG_PWM_DUTY)
 *  - Touch: k_work_delayable pollt REG_TOUCH_SCREEN_XY und meldet
 *    INPUT_ABS_X/Y + INPUT_BTN_TOUCH (fuer zephyr,lvgl-pointer-input)
 *
 * Bring-up laeuft mit gedrosseltem SPI-Takt (<= 11 MHz laut Datenblatt),
 * danach mit spi-max-frequency aus dem Devicetree (FT81x-Maximum 30 MHz).
 * Alle SPI-Zugriffe (Flush im LVGL-Thread, Touch im Workqueue-Kontext)
 * sind ueber einen Mutex serialisiert.
 */

#define DT_DRV_COMPAT protronic_ft813

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ft813, CONFIG_DISPLAY_LOG_LEVEL);

/* ── FT81x-Speicherkarte ─────────────────────────────────────────── */
#define RAM_G               0x000000UL /* 1 MiB Grafik-RAM, Framebuffer bei 0 */
#define RAM_DL              0x300000UL

#define REG_ID              0x302000UL
#define REG_CPURESET        0x302020UL
#define REG_HCYCLE          0x30202CUL
#define REG_HOFFSET         0x302030UL
#define REG_HSIZE           0x302034UL
#define REG_HSYNC0          0x302038UL
#define REG_HSYNC1          0x30203CUL
#define REG_VCYCLE          0x302040UL
#define REG_VOFFSET         0x302044UL
#define REG_VSIZE           0x302048UL
#define REG_VSYNC0          0x30204CUL
#define REG_VSYNC1          0x302050UL
#define REG_DLSWAP          0x302054UL
#define REG_DITHER          0x302060UL
#define REG_SWIZZLE         0x302064UL
#define REG_CSPREAD         0x302068UL
#define REG_PCLK_POL        0x30206CUL
#define REG_PCLK            0x302070UL
#define REG_GPIOX_DIR       0x302098UL
#define REG_GPIOX           0x30209CUL
#define REG_PWM_HZ          0x3020D0UL
#define REG_PWM_DUTY        0x3020D4UL
#define REG_TOUCH_MODE      0x302104UL
#define REG_CTOUCH_EXTENDED 0x302108UL
#define REG_TOUCH_SCREEN_XY 0x302124UL

/* ── Host-Kommandos (3-Byte-Rahmen) ──────────────────────────────── */
#define HCMD_ACTIVE    0x00
#define HCMD_CLKEXT    0x44
#define HCMD_CLKINT    0x48
#define HCMD_RST_PULSE 0x68

/* ── Panel-Timings: 4D Systems gen4-FT813-70 (Standard-WVGA-Satz) ── */
#define H_CYCLE  928
#define H_OFFSET 88
#define H_SYNC0  0
#define H_SYNC1  48
#define V_CYCLE  525
#define V_OFFSET 32
#define V_SYNC0  0
#define V_SYNC1  3
#define PCLK_DIV 2 /* 60 MHz / 2 = 30 MHz Pixeltakt */
#define PCLK_POL 1 /* fallende Flanke */
#define SWIZZLE  0
#define CSPREAD  0

/* ── Displaylisten-Kodierung (Teilmenge) ─────────────────────────── */
#define DL_CLEAR_COLOR_RGB(r, g, b) (0x02000000UL | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define DL_CLEAR()                  0x26000007UL /* Farbe + Stencil + Tag */
#define DL_BITMAP_HANDLE(h)         (0x05000000UL | ((uint32_t)(h) & 0x1FUL))
#define DL_BITMAP_SOURCE(addr)      (0x01000000UL | ((uint32_t)(addr) & 0x3FFFFFUL))
#define DL_BITMAP_LAYOUT(fmt, stride, h)                                                                              \
    (0x07000000UL | ((uint32_t)(fmt) << 19) | (((uint32_t)(stride) & 0x3FFUL) << 9) | ((uint32_t)(h) & 0x1FFUL))
#define DL_BITMAP_LAYOUT_H(stride, h)                                                                                 \
    (0x28000000UL | ((((uint32_t)(stride) >> 10) & 0x3UL) << 2) | (((uint32_t)(h) >> 9) & 0x3UL))
#define DL_BITMAP_SIZE(w, h) (0x08000000UL | (((uint32_t)(w) & 0x1FFUL) << 9) | ((uint32_t)(h) & 0x1FFUL))
#define DL_BITMAP_SIZE_H(w, h)                                                                                        \
    (0x29000000UL | ((((uint32_t)(w) >> 9) & 0x3UL) << 2) | (((uint32_t)(h) >> 9) & 0x3UL))
#define DL_BEGIN_BITMAPS() 0x1F000001UL
#define DL_VERTEX2II(x, y) (0x80000000UL | (((uint32_t)(x) & 0x1FFUL) << 21) | (((uint32_t)(y) & 0x1FFUL) << 12))
#define DL_END()           0x21000000UL
#define DL_DISPLAY()       0x00000000UL
#define DLSWAP_FRAME       2UL

/* Bring-up-Takt laut Datenblatt <= 11 MHz */
#define FT813_SLOW_HZ 8000000u

struct ft813_config {
    struct spi_dt_spec bus; /* Betriebstakt aus spi-max-frequency */
    struct gpio_dt_spec pd;
    uint16_t width;
    uint16_t height;
    uint8_t backlight_duty;
    uint16_t touch_poll_ms;
};

struct ft813_data {
    const struct device* dev;
    struct k_mutex lock;
    struct spi_config spi_slow; /* Kopie der Buskonfig mit Bring-up-Takt */
    bool ready;
    struct k_work_delayable touch_work;
    bool touch_pressed;
};

/* ── SPI-Grundoperationen ────────────────────────────────────────── */

static int
ft813_host_command(const struct device* dev, uint8_t cmd, bool slow) {
    const struct ft813_config* cfg = dev->config;
    struct ft813_data* data = dev->data;
    uint8_t frame[3] = {cmd, 0x00, 0x00};
    const struct spi_buf tx = {.buf = frame, .len = sizeof(frame)};
    const struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};

    return spi_transceive(cfg->bus.bus, slow ? &data->spi_slow : &cfg->bus.config, &tx_set, NULL);
}

/* Burst-Write nach addr (auto-inkrementierend); CS bleibt ueber
 * Header + Daten aktiv (eine SPI-Transaktion mit zwei Puffern). */
static int
ft813_wr_bytes(const struct device* dev, uint32_t addr, const uint8_t* buf, uint32_t len) {
    const struct ft813_config* cfg = dev->config;
    uint8_t hdr[3] = {
        (uint8_t)(((addr >> 16) & 0x3F) | 0x80),
        (uint8_t)(addr >> 8),
        (uint8_t)addr,
    };
    const struct spi_buf tx[2] = {
        {.buf = hdr, .len = sizeof(hdr)},
        {.buf = (uint8_t*)buf, .len = len},
    };
    const struct spi_buf_set tx_set = {.buffers = tx, .count = (len > 0) ? 2u : 1u};

    return spi_write_dt(&cfg->bus, &tx_set);
}

static int
ft813_rd_bytes(const struct device* dev, uint32_t addr, uint8_t* buf, uint32_t len, bool slow) {
    const struct ft813_config* cfg = dev->config;
    struct ft813_data* data = dev->data;
    /* 3 Adressbytes (Top-Bits 00 = Read) + 1 Dummy-Byte */
    uint8_t hdr[4] = {
        (uint8_t)((addr >> 16) & 0x3F),
        (uint8_t)(addr >> 8),
        (uint8_t)addr,
        0x00,
    };
    const struct spi_buf tx[2] = {
        {.buf = hdr, .len = sizeof(hdr)},
        {.buf = NULL, .len = len}, /* Taktbytes fuer die Antwort */
    };
    struct spi_buf rx[2] = {
        {.buf = NULL, .len = sizeof(hdr)}, /* Header-Antwort verwerfen */
        {.buf = buf, .len = len},
    };
    const struct spi_buf_set tx_set = {.buffers = tx, .count = 2};
    const struct spi_buf_set rx_set = {.buffers = rx, .count = 2};

    return spi_transceive(cfg->bus.bus, slow ? &data->spi_slow : &cfg->bus.config, &tx_set, &rx_set);
}

static int
ft813_wr8(const struct device* dev, uint32_t addr, uint8_t v) {
    return ft813_wr_bytes(dev, addr, &v, 1);
}

static int
ft813_wr16(const struct device* dev, uint32_t addr, uint16_t v) {
    uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    return ft813_wr_bytes(dev, addr, b, sizeof(b));
}

static int
ft813_wr32(const struct device* dev, uint32_t addr, uint32_t v) {
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    return ft813_wr_bytes(dev, addr, b, sizeof(b));
}

static int
ft813_rd32(const struct device* dev, uint32_t addr, uint32_t* v, bool slow) {
    uint8_t b[4];
    int ret = ft813_rd_bytes(dev, addr, b, sizeof(b), slow);
    if (ret == 0) {
        *v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    }
    return ret;
}

/* ── Bring-up (Port von ft81x_init/configure) ────────────────────── */

static void
ft813_power_cycle(const struct device* dev) {
    const struct ft813_config* cfg = dev->config;

    gpio_pin_set_dt(&cfg->pd, 1); /* PD asserted = Power-Down */
    k_msleep(6);
    gpio_pin_set_dt(&cfg->pd, 0);
    k_msleep(21);
    (void)ft813_host_command(dev, HCMD_RST_PULSE, true);
    k_msleep(21);
}

static bool
ft813_poll_chip_id(const struct device* dev) {
    for (int i = 0; i < 80; i++) {
        uint8_t id = 0;
        if (ft813_rd_bytes(dev, REG_ID, &id, 1, true) == 0 && id == 0x7C) {
            return true;
        }
        k_msleep(5);
    }
    return false;
}

static int
ft813_show_framebuffer(const struct device* dev) {
    const struct ft813_config* cfg = dev->config;
    const uint32_t stride = (uint32_t)cfg->width * 2u;
    const uint32_t dl[] = {
        DL_CLEAR_COLOR_RGB(0, 0, 0),
        DL_CLEAR(),
        DL_BITMAP_HANDLE(0),
        DL_BITMAP_SOURCE(RAM_G),
        DL_BITMAP_LAYOUT(7, stride, cfg->height), /* 7 = RGB565 */
        DL_BITMAP_LAYOUT_H(stride, cfg->height),
        DL_BITMAP_SIZE(cfg->width, cfg->height),
        DL_BITMAP_SIZE_H(cfg->width, cfg->height),
        DL_BEGIN_BITMAPS(),
        DL_VERTEX2II(0, 0),
        DL_END(),
        DL_DISPLAY(),
    };

    for (size_t i = 0; i < ARRAY_SIZE(dl); i++) {
        int ret = ft813_wr32(dev, RAM_DL + 4u * i, dl[i]);
        if (ret != 0) {
            return ret;
        }
    }
    return ft813_wr32(dev, REG_DLSWAP, DLSWAP_FRAME);
}

static int
ft813_configure(const struct device* dev) {
    const struct ft813_config* cfg = dev->config;
    uint32_t gpiox = 0;
    int ret;

    /* Warten bis die Grafik-Engine den Reset verlaesst */
    for (int i = 0;; i++) {
        uint8_t cpureset = 0xFF;
        ret = ft813_rd_bytes(dev, REG_CPURESET, &cpureset, 1, true);
        if (ret != 0) {
            return ret;
        }
        if (cpureset == 0) {
            break;
        }
        if (i >= 80) {
            return -ETIMEDOUT;
        }
        k_msleep(5);
    }

    /* Panel-Timings (REG_PCLK bleibt 0 = Display aus, bis ganz zum Schluss) */
    if ((ret = ft813_wr16(dev, REG_HSIZE, cfg->width)) != 0) return ret;
    if ((ret = ft813_wr16(dev, REG_HCYCLE, H_CYCLE)) != 0) return ret;
    if ((ret = ft813_wr16(dev, REG_HOFFSET, H_OFFSET)) != 0) return ret;
    if ((ret = ft813_wr16(dev, REG_HSYNC0, H_SYNC0)) != 0) return ret;
    if ((ret = ft813_wr16(dev, REG_HSYNC1, H_SYNC1)) != 0) return ret;
    if ((ret = ft813_wr16(dev, REG_VSIZE, cfg->height)) != 0) return ret;
    if ((ret = ft813_wr16(dev, REG_VCYCLE, V_CYCLE)) != 0) return ret;
    if ((ret = ft813_wr16(dev, REG_VOFFSET, V_OFFSET)) != 0) return ret;
    if ((ret = ft813_wr16(dev, REG_VSYNC0, V_SYNC0)) != 0) return ret;
    if ((ret = ft813_wr16(dev, REG_VSYNC1, V_SYNC1)) != 0) return ret;
    if ((ret = ft813_wr8(dev, REG_SWIZZLE, SWIZZLE)) != 0) return ret;
    if ((ret = ft813_wr8(dev, REG_PCLK_POL, PCLK_POL)) != 0) return ret;
    if ((ret = ft813_wr8(dev, REG_CSPREAD, CSPREAD)) != 0) return ret;
    if ((ret = ft813_wr8(dev, REG_DITHER, 1)) != 0) return ret;

    /* Kapazitiver Touch: Kompatibilitaetsmodus (Single-Touch), kontinuierlich */
    if ((ret = ft813_wr8(dev, REG_CTOUCH_EXTENDED, 1)) != 0) return ret;
    if ((ret = ft813_wr8(dev, REG_TOUCH_MODE, 3)) != 0) return ret;

    /* Leere Displayliste, damit das erste sichtbare Bild schwarz ist */
    if ((ret = ft813_wr32(dev, RAM_DL, DL_CLEAR_COLOR_RGB(0, 0, 0))) != 0) return ret;
    if ((ret = ft813_wr32(dev, RAM_DL + 4, DL_CLEAR())) != 0) return ret;
    if ((ret = ft813_wr32(dev, RAM_DL + 8, DL_DISPLAY())) != 0) return ret;
    if ((ret = ft813_wr32(dev, REG_DLSWAP, DLSWAP_FRAME)) != 0) return ret;

    /* DISP-Pin high (GPIOX Bit 15), Backlight-PWM bereit aber dunkel */
    if ((ret = ft813_wr16(dev, REG_GPIOX_DIR, 0x8000)) != 0) return ret;
    if ((ret = ft813_rd32(dev, REG_GPIOX, &gpiox, true)) != 0) return ret;
    if ((ret = ft813_wr16(dev, REG_GPIOX, (uint16_t)gpiox | 0x8000)) != 0) return ret;
    if ((ret = ft813_wr16(dev, REG_PWM_HZ, 250)) != 0) return ret;
    if ((ret = ft813_wr8(dev, REG_PWM_DUTY, 0)) != 0) return ret;

    /* Pixeltakt starten — das Panel scannt ab jetzt */
    return ft813_wr8(dev, REG_PCLK, PCLK_DIV);
}

/* Framebuffer schwarz initialisieren (zeilenweise) */
static int
ft813_clear_framebuffer(const struct device* dev) {
    const struct ft813_config* cfg = dev->config;
    uint8_t line[800 * 2] = {0};
    const uint32_t stride = (uint32_t)cfg->width * 2u;

    for (uint32_t y = 0; y < cfg->height; y++) {
        int ret = ft813_wr_bytes(dev, RAM_G + y * stride, line, stride);
        if (ret != 0) {
            return ret;
        }
    }
    return 0;
}

/* ── Zephyr-Display-API ──────────────────────────────────────────── */

static int
ft813_write(const struct device* dev, const uint16_t x, const uint16_t y, const struct display_buffer_descriptor* desc,
            const void* buf) {
    const struct ft813_config* cfg = dev->config;
    struct ft813_data* data = dev->data;
    const uint8_t* px = buf;
    const uint32_t stride = (uint32_t)cfg->width * 2u;
    const uint32_t row_bytes = (uint32_t)desc->width * 2u;
    int ret = 0;

    if (!data->ready) {
        return -EIO;
    }

    k_mutex_lock(&data->lock, K_FOREVER);
    if (x == 0 && desc->width == cfg->width && desc->pitch == desc->width) {
        /* Volle Breite -> zusammenhaengend im RAM_G: ein Burst */
        ret = ft813_wr_bytes(dev, RAM_G + (uint32_t)y * stride, px, row_bytes * desc->height);
    } else {
        for (uint32_t row = 0; row < desc->height && ret == 0; row++) {
            uint32_t addr = RAM_G + ((uint32_t)y + row) * stride + (uint32_t)x * 2u;
            ret = ft813_wr_bytes(dev, addr, px + row * (uint32_t)desc->pitch * 2u, row_bytes);
        }
    }
    k_mutex_unlock(&data->lock);
    return ret;
}

static int
ft813_blanking_off(const struct device* dev) {
    const struct ft813_config* cfg = dev->config;
    struct ft813_data* data = dev->data;
    int ret;

    k_mutex_lock(&data->lock, K_FOREVER);
    ret = ft813_wr8(dev, REG_PWM_DUTY, cfg->backlight_duty > 128 ? 128 : cfg->backlight_duty);
    k_mutex_unlock(&data->lock);
    return ret;
}

static int
ft813_blanking_on(const struct device* dev) {
    struct ft813_data* data = dev->data;
    int ret;

    k_mutex_lock(&data->lock, K_FOREVER);
    ret = ft813_wr8(dev, REG_PWM_DUTY, 0);
    k_mutex_unlock(&data->lock);
    return ret;
}

static int
ft813_set_brightness(const struct device* dev, const uint8_t brightness) {
    struct ft813_data* data = dev->data;
    int ret;

    k_mutex_lock(&data->lock, K_FOREVER);
    ret = ft813_wr8(dev, REG_PWM_DUTY, brightness / 2); /* 0..255 -> 0..127 */
    k_mutex_unlock(&data->lock);
    return ret;
}

static void
ft813_get_capabilities(const struct device* dev, struct display_capabilities* caps) {
    const struct ft813_config* cfg = dev->config;

    memset(caps, 0, sizeof(*caps));
    caps->x_resolution = cfg->width;
    caps->y_resolution = cfg->height;
    caps->supported_pixel_formats = PIXEL_FORMAT_RGB_565;
    caps->current_pixel_format = PIXEL_FORMAT_RGB_565;
    caps->current_orientation = DISPLAY_ORIENTATION_NORMAL;
}

static int
ft813_set_pixel_format(const struct device* dev, const enum display_pixel_format format) {
    ARG_UNUSED(dev);
    return (format == PIXEL_FORMAT_RGB_565) ? 0 : -ENOTSUP;
}

static DEVICE_API(display, ft813_display_api) = {
    .write = ft813_write,
    .blanking_on = ft813_blanking_on,
    .blanking_off = ft813_blanking_off,
    .set_brightness = ft813_set_brightness,
    .get_capabilities = ft813_get_capabilities,
    .set_pixel_format = ft813_set_pixel_format,
};

/* ── Touch-Poll (Input-Subsystem, fuer zephyr,lvgl-pointer-input) ── */

static void
ft813_touch_poll(struct k_work* work) {
    struct k_work_delayable* dwork = k_work_delayable_from_work(work);
    struct ft813_data* data = CONTAINER_OF(dwork, struct ft813_data, touch_work);
    const struct device* dev = data->dev;
    const struct ft813_config* cfg = dev->config;
    uint32_t xy = 0;
    int ret;

    k_mutex_lock(&data->lock, K_FOREVER);
    ret = ft813_rd32(dev, REG_TOUCH_SCREEN_XY, &xy, false);
    k_mutex_unlock(&data->lock);

    if (ret == 0) {
        if (xy == 0x80008000UL) {
            if (data->touch_pressed) {
                data->touch_pressed = false;
                input_report_key(dev, INPUT_BTN_TOUCH, 0, true, K_FOREVER);
            }
        } else {
            int32_t x = (int16_t)(xy >> 16);
            int32_t y = (int16_t)(xy & 0xFFFF);

            x = CLAMP(x, 0, (int32_t)cfg->width - 1);
            y = CLAMP(y, 0, (int32_t)cfg->height - 1);
            data->touch_pressed = true;
            input_report_abs(dev, INPUT_ABS_X, x, false, K_FOREVER);
            input_report_abs(dev, INPUT_ABS_Y, y, false, K_FOREVER);
            input_report_key(dev, INPUT_BTN_TOUCH, 1, true, K_FOREVER);
        }
    }

    k_work_schedule(dwork, K_MSEC(cfg->touch_poll_ms));
}

/* ── Init ────────────────────────────────────────────────────────── */

static int
ft813_init(const struct device* dev) {
    const struct ft813_config* cfg = dev->config;
    struct ft813_data* data = dev->data;
    static const uint8_t clk_cmds[2] = {HCMD_CLKINT, HCMD_CLKEXT};
    int ret = -EIO;

    if (!spi_is_ready_dt(&cfg->bus) || !gpio_is_ready_dt(&cfg->pd)) {
        LOG_ERR("SPI/GPIO nicht bereit");
        return -ENODEV;
    }

    data->dev = dev;
    k_mutex_init(&data->lock);

    /* Bring-up-Taktkopie (<= 11 MHz) der Buskonfiguration */
    data->spi_slow = cfg->bus.config;
    data->spi_slow.frequency = FT813_SLOW_HZ;

    ret = gpio_pin_configure_dt(&cfg->pd, GPIO_OUTPUT_ACTIVE);
    if (ret != 0) {
        return ret;
    }

    /* gen4-FT813-Module laufen vom internen Oszillator: erst CLKINT,
     * einmalig CLKEXT-Fallback (Modulvarianten mit Quarz). */
    for (size_t attempt = 0; attempt < 2; attempt++) {
        ft813_power_cycle(dev);
        if ((ret = ft813_host_command(dev, clk_cmds[attempt], true)) != 0) {
            return ret;
        }
        if ((ret = ft813_host_command(dev, HCMD_ACTIVE, true)) != 0) {
            return ret;
        }
        k_msleep(40);

        if (ft813_poll_chip_id(dev)) {
            ret = ft813_configure(dev);
            if (ret != 0) {
                return ret;
            }
            /* Ab hier Betriebstakt (spi-max-frequency) */
            (void)ft813_clear_framebuffer(dev);
            ret = ft813_show_framebuffer(dev);
            if (ret != 0) {
                return ret;
            }
            data->ready = true;

            if (IS_ENABLED(CONFIG_INPUT) && cfg->touch_poll_ms > 0) {
                k_work_init_delayable(&data->touch_work, ft813_touch_poll);
                k_work_schedule(&data->touch_work, K_MSEC(cfg->touch_poll_ms));
            }
            LOG_INF("FT813 initialisiert (%ux%u)", cfg->width, cfg->height);
            return 0;
        }
    }

    LOG_ERR("FT813: REG_ID meldet nie 0x7C (Verdrahtung/Takt?)");
    return -EIO;
}

#define FT813_DEFINE(inst)                                                                                            \
    static struct ft813_data ft813_data_##inst;                                                                       \
    static const struct ft813_config ft813_config_##inst = {                                                          \
        .bus = SPI_DT_SPEC_INST_GET(inst, SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0),                \
        .pd = GPIO_DT_SPEC_INST_GET(inst, pd_gpios),                                                                  \
        .width = DT_INST_PROP(inst, width),                                                                           \
        .height = DT_INST_PROP(inst, height),                                                                         \
        .backlight_duty = DT_INST_PROP(inst, backlight_duty),                                                         \
        .touch_poll_ms = DT_INST_PROP(inst, touch_poll_ms),                                                           \
    };                                                                                                                \
    DEVICE_DT_INST_DEFINE(inst, ft813_init, NULL, &ft813_data_##inst, &ft813_config_##inst, POST_KERNEL,              \
                          CONFIG_DISPLAY_INIT_PRIORITY, &ft813_display_api);

DT_INST_FOREACH_STATUS_OKAY(FT813_DEFINE)
