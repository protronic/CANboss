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

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/usb/usbd.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(canboss_usb, LOG_LEVEL_ERR);

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

    return 0;
}

bool
canboss_usb_take_attach(void) {
    return atomic_cas(&attach_pending, 1, 0);
}
