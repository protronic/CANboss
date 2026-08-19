/**
 * USB-CDC-Bring-up fuer das H573I-DK: Dead-Battery-Rd, TCPP03-M20,
 * dann USBD-Next + CDC-ACM. Zweite Shell auf der CDC nach DTR.
 */

#include "usb.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/usb/usbd.h>

#ifdef CONFIG_SOC_FAMILY_STM32
#include <stm32_ll_pwr.h>
#endif

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(usbdemo_usb, LOG_LEVEL_INF);

#define USB_DRD_BCDR 0x40016058U

#if defined(CONFIG_SOC_SERIES_STM32H5X) && defined(PWR_UCPDR_UCPD_DBDIS)
static int
usbdemo_keep_cc_rd(void) {
    LL_PWR_EnableUCPDDeadBattery();
    return 0;
}

SYS_INIT(usbdemo_keep_cc_rd, PRE_KERNEL_1, 0);
#endif

#if defined(CONFIG_BOARD_STM32H573I_DK)
#define TCPP_I2C_ADDR 0x34
#define TCPP_REG_CTRL 0x00
#define TCPP_REG_ACK  0x01
#define TCPP_REG_FLAG 0x02
#define TCPP_MODE_NORMAL 0x10

static void
usbdemo_enable_tcpp(void) {
    const struct device* en = DEVICE_DT_GET(DT_NODELABEL(gpiog));
    const struct device* i2c = DEVICE_DT_GET(DT_NODELABEL(i2c4));
    uint8_t ack = 0;
    uint8_t flag = 0;
    int err;

    if (device_is_ready(en)) {
        (void)gpio_pin_configure(en, 0, GPIO_OUTPUT_ACTIVE);
    }

    k_msleep(10);

    if (!device_is_ready(i2c)) {
        LOG_ERR("TCPP: I2C4 nicht bereit");
        return;
    }

    err = i2c_reg_write_byte(i2c, TCPP_I2C_ADDR, TCPP_REG_CTRL, TCPP_MODE_NORMAL);
    if (err) {
        LOG_ERR("TCPP: I2C-Write fehlgeschlagen (%d)", err);
        return;
    }

    (void)i2c_reg_read_byte(i2c, TCPP_I2C_ADDR, TCPP_REG_ACK, &ack);
    (void)i2c_reg_read_byte(i2c, TCPP_I2C_ADDR, TCPP_REG_FLAG, &flag);
    LOG_INF("TCPP: NORMAL (ack=0x%02x flag=0x%02x)%s", ack, flag,
            (flag & 0x20) ? " VBUS_OK" : " kein VBUS");
}
#else
static void
usbdemo_enable_tcpp(void) {
}
#endif

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

#if defined(CONFIG_SOC_SERIES_STM32H5X) && defined(PWR_UCPDR_UCPD_DBDIS)
    LL_PWR_EnableUCPDDeadBattery();
    LOG_INF("UCPD Dead-Battery Rd %s",
            LL_PWR_IsEnabledUCPDDeadBattery() ? "an" : "AUS");
#endif

    usbdemo_enable_tcpp();

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
