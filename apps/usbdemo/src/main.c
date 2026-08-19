/**
 * USB-Bring-up-Demo: CDC-ACM am User-USB (CN17) plus Zephyr-Shell und
 * Berry-GPIO. Kein CAN, kein Gateway, kein CANopen.
 *
 * ST-Link CN10 /dev/ttyACM0 @ 115200: Logs + uart:~$
 * User-USB CN17: VID/PID 1209:0001, zweite Shell usb:~$ nach DTR
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "berry_gpio.h"
#include "usb.h"

LOG_MODULE_REGISTER(usbdemo, LOG_LEVEL_INF);

int
main(void) {
    LOG_INF("USB-Demo startet (Shell + Berry, kein CAN)");

    usbdemo_berry_init();
    LOG_INF("Berry bereit: berry help()  oder  berry led(0, true)");

    int err = usbdemo_usb_init();
    if (err != 0) {
        LOG_ERR("USB-Init fehlgeschlagen (%d) - Shell bleibt auf ST-Link", err);
    }

    for (;;) {
        k_sleep(K_SECONDS(5));
    }
}
