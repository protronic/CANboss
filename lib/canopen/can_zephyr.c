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
#include <stddef.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/sys_io.h>

CAN_MSGQ_DEFINE(cb_can_rx_msgq, 32);

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

/* TX-Complete-Callback (ISR-Kontext): nur Fehler zaehlen. WICHTIG:
 * ohne Callback wartet can_send() per k_sem_take(..., K_FOREVER) auf
 * TX-Complete - also auf das Bus-ACK. Auf einem Bus ohne zweiten
 * Knoten (fehlender Transceiver, offener Stecker) retransmittiert der
 * Controller endlos und der sendende Thread haengt fuer immer: der
 * CANopen-Mainline-Thread beim ersten Heartbeat, der ndjson-Poll-
 * Thread beim ersten SLCAN-/gtwa-Frame. Deshalb asynchron senden -
 * "eingereiht" gilt als Erfolg, das Timeout begrenzt nur das Warten
 * auf einen freien TX-Mailbox-Platz. */
static atomic_t cb_tx_err_count;

static void
zephyr_tx_done(const struct device* dev, int error, void* user_data) {
    ARG_UNUSED(dev);
    ARG_UNUSED(user_data);
    if (error != 0) {
        atomic_inc(&cb_tx_err_count);
    }
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

    ret = can_send(cb_can_dev, &zf, K_MSEC(100), zephyr_tx_done, NULL);
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

#if DT_NODE_HAS_COMPAT(DT_CHOSEN(zephyr_canbus), st_stm32_fdcan)
/* Ein ECR-Lesezugriff (TEC/REC/RP/CEL). PSR.EW/EP unterscheidet TX
 * und RX nicht; die Zaehler schon. Lesen loescht CEL, daher Cache
 * fuer update_od / OD 0x2011. */
static uint8_t fdcan_tec;
static uint8_t fdcan_rec;
static uint8_t fdcan_rp;
static uint8_t fdcan_ecr_valid;
static atomic_t fdcan_cel_accum;

static void
fdcan_read_ecr(uint8_t* tec, uint8_t* rec, bool* rp, uint8_t* cel) {
    const uintptr_t ecr_addr =
        DT_REG_ADDR(DT_CHOSEN(zephyr_canbus)) + offsetof(FDCAN_GlobalTypeDef, ECR);
    uint32_t ecr = sys_read32((mem_addr_t)ecr_addr);

    *tec = (uint8_t)((ecr & FDCAN_ECR_TEC) >> FDCAN_ECR_TEC_Pos);
    *rec = (uint8_t)((ecr & FDCAN_ECR_REC) >> FDCAN_ECR_REC_Pos);
    *rp = (ecr & FDCAN_ECR_RP) != 0U;
    *cel = (uint8_t)((ecr & FDCAN_ECR_CEL) >> FDCAN_ECR_CEL_Pos);
}

int
cb_can_zephyr_take_ecr(uint8_t* tec, uint8_t* rec, bool* rp, uint8_t* cel) {
    atomic_val_t acc;

    if (fdcan_ecr_valid == 0U) {
        return -1;
    }
    if (tec != NULL) {
        *tec = fdcan_tec;
    }
    if (rec != NULL) {
        *rec = fdcan_rec;
    }
    if (rp != NULL) {
        *rp = fdcan_rp != 0U;
    }
    acc = atomic_set(&fdcan_cel_accum, 0);
    if (cel != NULL) {
        *cel = acc > 255 ? (uint8_t)255 : (uint8_t)acc;
    }
    return 0;
}
#else
int
cb_can_zephyr_take_ecr(uint8_t* tec, uint8_t* rec, bool* rp, uint8_t* cel) {
    (void)tec;
    (void)rec;
    (void)rp;
    (void)cel;
    return -1;
}
#endif /* st_stm32_fdcan */

static int
zephyr_get_errors(cb_can_err_t* err) {
    enum can_state state;

    if (err == NULL) {
        errno = EINVAL;
        return -1;
    }

    err->overflow = 0U;

    if (!device_is_ready(cb_can_dev)) {
        errno = ENODEV;
        return -1;
    }

#if DT_NODE_HAS_COMPAT(DT_CHOSEN(zephyr_canbus), st_stm32_fdcan)
    {
        uint8_t tec;
        uint8_t rec;
        uint8_t cel;
        bool rp;

        /* PSR nur fuer Bus-Off (TEC ist 8 Bit, saturiert bei 255). */
        if (can_get_state(cb_can_dev, &state, NULL) != 0) {
            errno = EIO;
            return -1;
        }
        fdcan_read_ecr(&tec, &rec, &rp, &cel);
        fdcan_tec = tec;
        fdcan_rec = rec;
        fdcan_rp = rp ? 1U : 0U;
        fdcan_ecr_valid = 1U;
        if (cel != 0U) {
            atomic_val_t prev = atomic_add(&fdcan_cel_accum, (atomic_val_t)cel);
            if (prev + (atomic_val_t)cel > 255) {
                atomic_set(&fdcan_cel_accum, 255);
            }
        }

        /* Getrennte TX-/RX-Bits: TEC und REC (RP = RX-Passive, REC=127). */
        err->tx_errors = tec;
        err->rx_errors = rp ? 128U : rec;
        if (state == CAN_STATE_BUS_OFF) {
            err->tx_errors = 256U;
        }
    }
#else
    {
        struct can_bus_err_cnt err_cnt;

        if (can_get_state(cb_can_dev, &state, &err_cnt) != 0) {
            errno = EIO;
            return -1;
        }
        err->tx_errors = err_cnt.tx_err_cnt;
        err->rx_errors = err_cnt.rx_err_cnt;
        if (state == CAN_STATE_BUS_OFF) {
            err->tx_errors = 256U;
        }
    }
#endif

    return 0;
}

const cb_can_backend_t cb_can_zephyr = {
    .name = "zephyr",
    .open = zephyr_open,
    .close = zephyr_close,
    .send = zephyr_send,
    .recv = zephyr_recv,
    .get_errors = zephyr_get_errors,
};
