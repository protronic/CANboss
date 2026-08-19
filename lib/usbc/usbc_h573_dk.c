/**
 * usbc_h573_dk.c - USB-C-Bring-up CN17 (TCPP03 in den NORMAL-Modus),
 * siehe usbc_h573_dk.h.
 */

#include "usbc_h573_dk.h"

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(canboss_usbc, LOG_LEVEL_INF);

#if defined(CONFIG_BOARD_STM32H573I_DK)

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>

#define TCPP_I2C_ADDR    0x34
#define TCPP_REG_CTRL    0x00
#define TCPP_REG_ACK     0x01
#define TCPP_REG_FLAG    0x02
#define TCPP_MODE_NORMAL 0x10
#define TCPP_FLAG_VBUS_OK 0x20

int
canboss_usbc_prepare(void) {
    const struct device* en = DEVICE_DT_GET(DT_NODELABEL(gpiog));
    const struct device* i2c = DEVICE_DT_GET(DT_NODELABEL(i2c4));
    uint8_t ack = 0;
    uint8_t flag = 0;
    int err;

    if (device_is_ready(en)) {
        (void)gpio_pin_configure(en, 0, GPIO_OUTPUT_ACTIVE); /* PG0 = TCPP-Enable */
    } else {
        LOG_ERR("TCPP: GPIOG nicht bereit");
    }

    /* VCC-Rampe des TCPP abwarten, bevor I2C spricht */
    k_msleep(10);

    if (!device_is_ready(i2c)) {
        LOG_ERR("TCPP: I2C4 nicht bereit");
        return -ENODEV;
    }

    err = i2c_reg_write_byte(i2c, TCPP_I2C_ADDR, TCPP_REG_CTRL, TCPP_MODE_NORMAL);
    if (err) {
        LOG_ERR("TCPP: I2C-Write fehlgeschlagen (%d)", err);
        return err;
    }

    (void)i2c_reg_read_byte(i2c, TCPP_I2C_ADDR, TCPP_REG_ACK, &ack);
    (void)i2c_reg_read_byte(i2c, TCPP_I2C_ADDR, TCPP_REG_FLAG, &flag);
    LOG_INF("TCPP: NORMAL (ack=0x%02x flag=0x%02x)%s", ack, flag,
            (flag & TCPP_FLAG_VBUS_OK) ? " VBUS_OK" : " kein VBUS");
    return 0;
}

#else /* !CONFIG_BOARD_STM32H573I_DK */

int
canboss_usbc_prepare(void) {
    return 0;
}

#endif
