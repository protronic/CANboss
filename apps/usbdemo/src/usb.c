/**
 * USB-CDC-Bring-up fuer das H573I-DK: Dead-Battery-Rd, TCPP03-M20,
 * dann USBD-Next + CDC-ACM. Zweite Shell auf der CDC nach DTR.
 */

#include "usb.h"

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/usb/usbd.h>

#include "usbc_h573_dk.h" /* Dead-Battery-Rd + TCPP03 (CN17) */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(usbdemo_usb, LOG_LEVEL_INF);

#define USB_DRD_BCDR 0x40016058U

static const char* const class_blocklist[] = {
    "dfu_dfu",
    NULL,
};

USBD_DEVICE_DEFINE(demo_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 0x1209, 0x0001);
USBD_DESC_LANG_DEFINE(demo_lang);
USBD_DESC_MANUFACTURER_DEFINE(demo_mfr, "Protronic GmbH");
USBD_DESC_PRODUCT_DEFINE(demo_product, "CANboss USB Demo");
USBD_DESC_SERIAL_NUMBER_DEFINE(demo_sn);
USBD_DESC_CONFIG_DEFINE(demo_fs_cfg_desc, "USB Demo");
USBD_CONFIGURATION_DEFINE(demo_fs_config, 0, 125, &demo_fs_cfg_desc);

static atomic_t cdc_shell_started;

SHELL_UART_DEFINE(cdc_transport);
SHELL_DEFINE(cdc_shell, "usb:~$ ", &cdc_transport, 256, 100, SHELL_FLAG_OLF_CRLF);

static void
usbdemo_start_cdc_shell(void) {
    const struct device* cdc = DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0));
    static const struct shell_backend_config_flags cfg_flags =
        SHELL_DEFAULT_BACKEND_CONFIG_FLAGS;

    if (!atomic_cas(&cdc_shell_started, 0, 1)) {
        return;
    }

    if (!device_is_ready(cdc)) {
        LOG_ERR("CDC-UART nicht bereit");
        return;
    }

    if (shell_init(&cdc_shell, cdc, cfg_flags, false, 0) != 0) {
        LOG_ERR("CDC-Shell init fehlgeschlagen");
        return;
    }

    if (shell_start(&cdc_shell) != 0) {
        LOG_ERR("CDC-Shell start fehlgeschlagen");
        return;
    }

    LOG_INF("CDC-Shell bereit (usb:~$)");
}

static void
usbd_msg_cb(struct usbd_context* const ctx, const struct usbd_msg* msg) {
    LOG_INF("USBD: %s", usbd_msg_type_string(msg->type));

    if (usbd_can_detect_vbus(ctx)) {
        if (msg->type == USBD_MSG_VBUS_READY) {
            (void)usbd_enable(ctx);
        }
        if (msg->type == USBD_MSG_VBUS_REMOVED) {
            (void)usbd_disable(ctx);
        }
    }

    if (msg->type == USBD_MSG_CONFIGURATION ||
        msg->type == USBD_MSG_CDC_ACM_CONTROL_LINE_STATE) {
        usbdemo_start_cdc_shell();
    }
}

int
usbdemo_usb_init(void) {
    int err;

    (void)canboss_usbc_prepare();

    if (!device_is_ready(DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)))) {
        LOG_ERR("UDC nicht bereit");
        return -ENODEV;
    }

    err = usbd_add_descriptor(&demo_usbd, &demo_lang);
    if (err == 0) {
        err = usbd_add_descriptor(&demo_usbd, &demo_mfr);
    }
    if (err == 0) {
        err = usbd_add_descriptor(&demo_usbd, &demo_product);
    }
    if (err == 0) {
        err = usbd_add_descriptor(&demo_usbd, &demo_sn);
    }
    if (err) {
        LOG_ERR("Deskriptor abgelehnt (%d)", err);
        return err;
    }

    err = usbd_add_configuration(&demo_usbd, USBD_SPEED_FS, &demo_fs_config);
    if (err) {
        LOG_ERR("Konfiguration abgelehnt (%d)", err);
        return err;
    }

    err = usbd_register_all_classes(&demo_usbd, USBD_SPEED_FS, 1, class_blocklist);
    if (err) {
        LOG_ERR("Klassen-Registrierung fehlgeschlagen (%d)", err);
        return err;
    }

    (void)usbd_device_set_code_triple(&demo_usbd, USBD_SPEED_FS, USB_BCC_MISCELLANEOUS, 0x02,
                                      0x01);
    usbd_self_powered(&demo_usbd, false);

    err = usbd_msg_register_cb(&demo_usbd, usbd_msg_cb);
    if (err) {
        LOG_ERR("Message-Callback abgelehnt (%d)", err);
        return err;
    }

    err = usbd_init(&demo_usbd);
    if (err) {
        LOG_ERR("usbd_init fehlgeschlagen (%d)", err);
        return err;
    }

    if (!usbd_can_detect_vbus(&demo_usbd)) {
        err = usbd_enable(&demo_usbd);
        if (err) {
            LOG_ERR("usbd_enable fehlgeschlagen (%d)", err);
            return err;
        }
    }

    LOG_INF("USB-Device aktiv (VID/PID 1209:0001) BCDR=0x%08x DPPU=%d",
            sys_read32(USB_DRD_BCDR), !!(sys_read32(USB_DRD_BCDR) & USB_BCDR_DPPU));
    return 0;
}

void
usbdemo_usb_start_cdc_shell(void) {
    usbdemo_start_cdc_shell();
}
