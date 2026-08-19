/**
 * usb_cdc.c - USB-Device-Setup (CDC-ACM) fuer das NDJSON-Gateway,
 * siehe usb_cdc.h.
 *
 * Aufbau wie in Zephyrs USB-Device-Next-Beispielen: ein Kontext, eine
 * Full-Speed-Konfiguration, alle im Devicetree instanziierten Klassen
 * (hier genau die cdc_acm_uart0 aus boards/stm32h573i_dk.overlay).
 * Die Seriennummer kommt aus hwinfo (STM32-UID) - so unterscheidet der
 * Browser mehrere Gateways am selben Rechner.
 */

#include "usb_cdc.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/usb/usbd.h>

#ifdef CONFIG_SOC_FAMILY_STM32
#include <stm32_ll_pwr.h>
#endif

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(canboss_usb, LOG_LEVEL_INF);

#if defined(CONFIG_SOC_SERIES_STM32H5X) && defined(PWR_UCPDR_UCPD_DBDIS)
/* Zephyr v4.4.2 schaltet die Rd-Pull-downs in soc_early_init_hook() ab,
 * weil die Bedingung nur CONFIG_USB_DEVICE_DRIVER kennt. Ohne Rd sieht
 * ein Type-C-Host keinen Sink. Vor jedem USB-Init wieder einschalten. */
static int
canboss_usb_keep_cc_rd(void) {
    LL_PWR_EnableUCPDDeadBattery();
    return 0;
}

SYS_INIT(canboss_usb_keep_cc_rd, PRE_KERNEL_1, 0);
#endif

#if defined(CONFIG_BOARD_STM32H573I_DK)
/* STM32H573I-DK CN17: TCPP03-M20 (TCPP0203) auf I2C4 @ 0x34, VCC an PG0.
 * Ohne NORMAL-Modus bleiben die CC-Schalter zu - der Host legt ggf. VBUS
 * an (LD7), sieht aber kein Device auf D+/D-. TinyUSB/Cube machen dasselbe. */
#define CANBOSS_TCPP_I2C_ADDR 0x34
#define CANBOSS_TCPP_REG_CTRL 0x00
#define CANBOSS_TCPP_REG_ACK  0x01
#define CANBOSS_TCPP_REG_FLAG 0x02
#define CANBOSS_TCPP_MODE_NORMAL 0x10

static void
canboss_usb_enable_tcpp(void) {
    const struct device* en = DEVICE_DT_GET(DT_NODELABEL(gpiog));
    const struct device* i2c = DEVICE_DT_GET(DT_NODELABEL(i2c4));
    uint8_t ack = 0;
    uint8_t flag = 0;
    int err;

    if (device_is_ready(en)) {
        (void)gpio_pin_configure(en, 0, GPIO_OUTPUT_ACTIVE);
    } else {
        LOG_ERR("TCPP: GPIOG nicht bereit");
    }

    /* VCC-Rampe des TCPP, bevor I2C spricht */
    k_msleep(10);

    if (!device_is_ready(i2c)) {
        LOG_ERR("TCPP: I2C4 nicht bereit");
        return;
    }

    err = i2c_reg_write_byte(i2c, CANBOSS_TCPP_I2C_ADDR, CANBOSS_TCPP_REG_CTRL,
                             CANBOSS_TCPP_MODE_NORMAL);
    if (err) {
        LOG_ERR("TCPP: I2C-Write fehlgeschlagen (%d)", err);
        return;
    }

    (void)i2c_reg_read_byte(i2c, CANBOSS_TCPP_I2C_ADDR, CANBOSS_TCPP_REG_ACK, &ack);
    (void)i2c_reg_read_byte(i2c, CANBOSS_TCPP_I2C_ADDR, CANBOSS_TCPP_REG_FLAG, &flag);
    LOG_INF("TCPP: NORMAL (ack=0x%02x flag=0x%02x)", ack, flag);
}
#else
static void
canboss_usb_enable_tcpp(void) {
}
#endif

/* Die DFU-Mode-Instanz nie automatisch registrieren (wie in Zephyrs
 * Beispielen); das Gateway bringt ohnehin nur CDC-ACM mit. */
static const char* const class_blocklist[] = {
    "dfu_dfu",
    NULL,
};

USBD_DEVICE_DEFINE(gw_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), CONFIG_CANBOSS_GW_USB_VID,
                   CONFIG_CANBOSS_GW_USB_PID);

USBD_DESC_LANG_DEFINE(gw_lang);
USBD_DESC_MANUFACTURER_DEFINE(gw_mfr, CONFIG_CANBOSS_GW_USB_MANUFACTURER);
USBD_DESC_PRODUCT_DEFINE(gw_product, CONFIG_CANBOSS_GW_USB_PRODUCT);
USBD_DESC_SERIAL_NUMBER_DEFINE(gw_sn);
USBD_DESC_CONFIG_DEFINE(gw_fs_cfg_desc, "CANboss Gateway (NDJSON)");

/* Bus-powered gemeldet: das DK kann sowohl ueber ST-Link als auch ueber
 * den USB-FS-Stecker versorgt werden - die Angabe passt in beiden
 * Faellen, waehrend "self-powered" bei Busversorgung falsch waere. */
USBD_CONFIGURATION_DEFINE(gw_fs_config, 0, CONFIG_CANBOSS_GW_USB_MAX_POWER, &gw_fs_cfg_desc);

/* DTR-Flanke: der Host hat den Port geoeffnet (Webapp verbunden) */
static atomic_t attach_pending;

static void
usbd_msg_cb(struct usbd_context* const ctx, const struct usbd_msg* msg) {
    /* Enumerations-Fortschritt auf der Konsole (ST-Link-VCP):
     * reset -> configuration -> cdc_acm_line_coding usw. */
    LOG_INF("USBD: %s", usbd_msg_type_string(msg->type));

    if (usbd_can_detect_vbus(ctx)) {
        if (msg->type == USBD_MSG_VBUS_READY) {
            (void)usbd_enable(ctx);
        }
        if (msg->type == USBD_MSG_VBUS_REMOVED) {
            (void)usbd_disable(ctx);
        }
    }

    if (msg->type == USBD_MSG_CDC_ACM_CONTROL_LINE_STATE) {
        uint32_t dtr = 0U;

        (void)uart_line_ctrl_get(msg->dev, UART_LINE_CTRL_DTR, &dtr);
        if (dtr) {
            atomic_set(&attach_pending, 1);
        }
    }
}

int
canboss_usb_init(void) {
    int err;

#if defined(CONFIG_SOC_SERIES_STM32H5X) && defined(PWR_UCPDR_UCPD_DBDIS)
    LL_PWR_EnableUCPDDeadBattery();
    LOG_INF("UCPD Dead-Battery Rd %s",
            LL_PWR_IsEnabledUCPDDeadBattery() ? "an" : "AUS");
#endif

    canboss_usb_enable_tcpp();

    if (!device_is_ready(DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)))) {
        LOG_ERR("UDC-Controller nicht bereit");
        return -ENODEV;
    }

    err = usbd_add_descriptor(&gw_usbd, &gw_lang);
    if (err == 0) {
        err = usbd_add_descriptor(&gw_usbd, &gw_mfr);
    }
    if (err == 0) {
        err = usbd_add_descriptor(&gw_usbd, &gw_product);
    }
    if (err == 0) {
        err = usbd_add_descriptor(&gw_usbd, &gw_sn);
    }
    if (err) {
        LOG_ERR("Deskriptor abgelehnt (%d)", err);
        return err;
    }

    err = usbd_add_configuration(&gw_usbd, USBD_SPEED_FS, &gw_fs_config);
    if (err) {
        LOG_ERR("Full-Speed-Konfiguration abgelehnt (%d)", err);
        return err;
    }

    err = usbd_register_all_classes(&gw_usbd, USBD_SPEED_FS, 1, class_blocklist);
    if (err) {
        LOG_ERR("Klassen-Registrierung fehlgeschlagen (%d)", err);
        return err;
    }

    /* CDC-ACM besteht aus zwei Interfaces und bringt eine IAD mit:
     * Klassencode im Device-Deskriptor auf "Miscellaneous / Common
     * Class / IAD", damit Windows die Funktion korrekt zusammenfasst. */
    (void)usbd_device_set_code_triple(&gw_usbd, USBD_SPEED_FS, USB_BCC_MISCELLANEOUS, 0x02, 0x01);
    usbd_self_powered(&gw_usbd, false);

    err = usbd_msg_register_cb(&gw_usbd, usbd_msg_cb);
    if (err) {
        LOG_ERR("Message-Callback abgelehnt (%d)", err);
        return err;
    }

    err = usbd_init(&gw_usbd);
    if (err) {
        LOG_ERR("usbd_init fehlgeschlagen (%d)", err);
        return err;
    }

    /* Ohne VBUS-Erkennung (STM32-UDC) direkt einschalten; sonst
     * uebernimmt das der USBD_MSG_VBUS_READY-Pfad im Callback. */
    if (!usbd_can_detect_vbus(&gw_usbd)) {
        err = usbd_enable(&gw_usbd);
        if (err) {
            LOG_ERR("usbd_enable fehlgeschlagen (%d)", err);
            return err;
        }
    }

    LOG_INF("USB-Device aktiv (VID/PID %04x:%04x), warte auf Host", CONFIG_CANBOSS_GW_USB_VID,
            CONFIG_CANBOSS_GW_USB_PID);
    return 0;
}

bool
canboss_usb_take_attach(void) {
    return atomic_cas(&attach_pending, 1, 0);
}
