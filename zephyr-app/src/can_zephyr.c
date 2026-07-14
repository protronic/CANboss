/**
 * can_zephyr.c
 *
 * CAN-Backend auf der Zephyr-CAN-API (chosen zephyr,canbus).
 *
 * Auf native_sim bindet der Treiber "zephyr,native-linux-can" das
 * Host-SocketCAN-Interface an (boards/native_sim.overlay:
 * host-interface = "vcan0") - damit entspricht der native_sim-Build
 * funktional der POSIX-SocketCAN-Variante. Auf Hardware haengt hier
 * der jeweilige CAN-Controller aus dem Devicetree.
 */

#include "can_if.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>

CAN_MSGQ_DEFINE(cb_can_rx_msgq, 64);

static const struct device* const cb_can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
static int cb_filter_id = -1;

static int
zephyr_open(const char* device, uint32_t bitrate) {
    int ret;

    (void)device; /* Geraet kommt aus dem Devicetree (chosen zephyr,canbus) */

    if (!device_is_ready(cb_can_dev)) {
        errno = ENODEV;
        return -1;
    }

    if (bitrate > 0) {
        struct can_timing timing;
        if (can_calc_timing(cb_can_dev, &timing, bitrate, 875) >= 0) {
            /* Fehler ignorieren: native-linux-can uebernimmt die
             * Bitrate vom Host-Interface */
            (void)can_set_timing(cb_can_dev, &timing);
        }
    }

#if DT_NODE_HAS_COMPAT(DT_CHOSEN(zephyr_canbus), zephyr_can_loopback)
    /* Der virtuelle Loopback-Treiber stellt Frames nur im
     * Loopback-Modus zu (eigener SDO-Server antwortet dem eigenen
     * SDO-Client - Demo-Betrieb ohne Bus). */
    (void)can_set_mode(cb_can_dev, CAN_MODE_LOOPBACK);
#endif

    ret = can_start(cb_can_dev);
    if (ret != 0 && ret != -EALREADY) {
        errno = -ret;
        return -1;
    }

    /* Alle klassischen 11-Bit-Frames empfangen; verteilt wird in
     * CO_CANrxDispatch (Software-Filter des CANopenNode-Ports). */
    struct can_filter filter = {
        .id = 0,
        .mask = 0,
        .flags = 0,
    };
    cb_filter_id = can_add_rx_filter_msgq(cb_can_dev, &cb_can_rx_msgq, &filter);
    if (cb_filter_id < 0) {
        errno = -cb_filter_id;
        (void)can_stop(cb_can_dev);
        return -1;
    }

    return 0;
}

static void
zephyr_close(void) {
    if (cb_filter_id >= 0) {
        can_remove_rx_filter(cb_can_dev, cb_filter_id);
        cb_filter_id = -1;
    }
    (void)can_stop(cb_can_dev);
}

static int
zephyr_send(const cb_can_frame_t* frame) {
    struct can_frame zf;
    int ret;

    memset(&zf, 0, sizeof(zf));
    zf.id = frame->id;
    zf.dlc = frame->dlc > 8 ? 8 : frame->dlc;
    if (frame->rtr) {
        zf.flags |= CAN_FRAME_RTR;
    }
    memcpy(zf.data, frame->data, zf.dlc);

    ret = can_send(cb_can_dev, &zf, K_MSEC(100), NULL, NULL);
    if (ret != 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

static int
zephyr_recv(cb_can_frame_t* frame, int timeout_ms) {
    struct can_frame zf;
    k_timeout_t timeout = timeout_ms < 0 ? K_FOREVER : K_MSEC(timeout_ms);

    if (k_msgq_get(&cb_can_rx_msgq, &zf, timeout) != 0) {
        return 0;
    }
    if ((zf.flags & (CAN_FRAME_IDE | CAN_FRAME_FDF)) != 0) {
        return 0; /* nur klassische 11-Bit-Frames */
    }

    frame->id = (uint16_t)(zf.id & 0x7FFu);
    frame->rtr = (zf.flags & CAN_FRAME_RTR) != 0;
    frame->dlc = zf.dlc > 8 ? 8 : zf.dlc;
    memcpy(frame->data, zf.data, frame->dlc);
    return 1;
}

const cb_can_backend_t cb_can_zephyr = {
    .name = "zephyr",
    .open = zephyr_open,
    .close = zephyr_close,
    .send = zephyr_send,
    .recv = zephyr_recv,
};
