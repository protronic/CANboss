/**
 * can_socketcan.c
 *
 * SocketCAN-Backend (Linux). Vorbild: CANbossTouch/host/canboss_poc_can_host.c.
 *
 * Die Bitrate wird nicht hier gesetzt, sondern am Interface konfiguriert:
 *   sudo ip link set can0 type can bitrate 125000 && sudo ip link set can0 up
 * Fuer Tests ohne Hardware genuegt ein virtuelles Interface:
 *   sudo ip link add dev vcan0 type vcan && sudo ip link set vcan0 up
 */

#include "can_if.h"

#include <errno.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <linux/can.h>
#include <linux/can/error.h>
#include <linux/can/raw.h>

static int can_fd = -1;

/* Letzter bekannter Buszustand aus Errorframes (RX-Thread schreibt,
 * Mainline liest in get_errors). */
static volatile uint16_t sc_tx_errors;
static volatile uint16_t sc_rx_errors;
static volatile uint16_t sc_overflow;

static void
socketcan_err_reset(void) {
    sc_tx_errors = 0;
    sc_rx_errors = 0;
    sc_overflow = 0;
}

/* Errorframe auf TEC/REC/Overflow abbilden (Schwellen wie Blank-Treiber). */
static void
socketcan_note_err(const struct can_frame* cf) {
    uint32_t cls = cf->can_id & CAN_ERR_MASK;
    uint16_t tx = sc_tx_errors;
    uint16_t rx = sc_rx_errors;
    uint16_t ov = sc_overflow;

    if ((cls & CAN_ERR_CRTL) != 0U) {
        uint8_t c = cf->data[1];

        if ((c & CAN_ERR_CRTL_RX_OVERFLOW) != 0U && ov < 0xFFU) {
            ov++;
        }
        if ((c & CAN_ERR_CRTL_ACTIVE) != 0U) {
            tx = 0U;
            rx = 0U;
        } else {
            if ((c & CAN_ERR_CRTL_TX_PASSIVE) != 0U) {
                if (tx < 128U) {
                    tx = 128U;
                }
            } else if ((c & CAN_ERR_CRTL_TX_WARNING) != 0U) {
                if (tx < 96U) {
                    tx = 96U;
                }
            }
            if ((c & CAN_ERR_CRTL_RX_PASSIVE) != 0U) {
                if (rx < 128U) {
                    rx = 128U;
                }
            } else if ((c & CAN_ERR_CRTL_RX_WARNING) != 0U) {
                if (rx < 96U) {
                    rx = 96U;
                }
            }
        }
    }

#ifdef CAN_ERR_CNT
    if ((cls & CAN_ERR_CNT) != 0U) {
        tx = cf->data[6];
        rx = cf->data[7];
    }
#endif

    if ((cls & CAN_ERR_BUSOFF) != 0U) {
        tx = 256U;
    } else if ((cls & CAN_ERR_RESTARTED) != 0U && tx >= 256U) {
        tx = 0U;
    }

    sc_tx_errors = tx;
    sc_rx_errors = rx;
    sc_overflow = ov;
}

static int
socketcan_open(const char* device, uint32_t bitrate) {
    struct ifreq ifr;
    struct sockaddr_can addr;

    (void)bitrate; /* Bitrate kommt vom Interface (ip link) */

    can_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (can_fd < 0) {
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", device);
    if (ioctl(can_fd, SIOCGIFINDEX, &ifr) < 0) {
        close(can_fd);
        can_fd = -1;
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(can_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(can_fd);
        can_fd = -1;
        return -1;
    }

    /* Ohne ERR_FILTER liefert SocketCAN keine Errorframes. BUSERROR
     * absichtlich aus: die Frames koennen den RX-Pfad fluten. */
    {
        can_err_mask_t err_mask = (CAN_ERR_CRTL | CAN_ERR_BUSOFF | CAN_ERR_RESTARTED);
#ifdef CAN_ERR_CNT
        err_mask |= CAN_ERR_CNT;
#endif
        (void)setsockopt(can_fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &err_mask, sizeof(err_mask));
    }

    socketcan_err_reset();
    return 0;
}

static void
socketcan_close(void) {
    if (can_fd >= 0) {
        close(can_fd);
        can_fd = -1;
    }
    socketcan_err_reset();
}

static int
socketcan_send(const cb_can_frame_t* frame) {
    struct can_frame cf;

    if (can_fd < 0) {
        errno = EBADF;
        return -1;
    }

    memset(&cf, 0, sizeof(cf));
    cf.can_id = frame->id & CAN_SFF_MASK;
    if (frame->rtr) {
        cf.can_id |= CAN_RTR_FLAG;
    }
    cf.can_dlc = frame->dlc > 8 ? 8 : frame->dlc;
    memcpy(cf.data, frame->data, cf.can_dlc);

    for (;;) {
        ssize_t n = write(can_fd, &cf, sizeof(cf));
        if (n == (ssize_t)sizeof(cf)) {
            return 0;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && errno == ENOBUFS) {
            /* TX-Queue voll: kurz warten und erneut versuchen */
            usleep(200);
            continue;
        }
        return -1;
    }
}

static int
socketcan_recv(cb_can_frame_t* frame, int timeout_ms) {
    struct can_frame cf;
    fd_set rfds;
    struct timeval tv;

    if (can_fd < 0) {
        errno = EBADF;
        return -1;
    }

    FD_ZERO(&rfds);
    FD_SET(can_fd, &rfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int sel = select(can_fd + 1, &rfds, NULL, NULL, timeout_ms < 0 ? NULL : &tv);
    if (sel < 0) {
        if (errno == EINTR) {
            return 0;
        }
        return -1;
    }
    if (sel == 0) {
        return 0;
    }

    ssize_t n = read(can_fd, &cf, sizeof(cf));
    if (n < 0) {
        if (errno == EINTR || errno == EAGAIN) {
            return 0;
        }
        return -1;
    }
    if (n < (ssize_t)sizeof(cf) || (cf.can_id & CAN_EFF_FLAG)) {
        return 0; /* nur klassische 11-Bit-Frames */
    }
    if ((cf.can_id & CAN_ERR_FLAG) != 0U) {
        socketcan_note_err(&cf);
        return 0;
    }

    frame->id = (uint16_t)(cf.can_id & CAN_SFF_MASK);
    frame->rtr = (cf.can_id & CAN_RTR_FLAG) != 0;
    frame->dlc = cf.can_dlc > 8 ? 8 : cf.can_dlc;
    memcpy(frame->data, cf.data, frame->dlc);
    return 1;
}

static int
socketcan_get_errors(cb_can_err_t* err) {
    if (err == NULL) {
        errno = EINVAL;
        return -1;
    }
    err->tx_errors = sc_tx_errors;
    err->rx_errors = sc_rx_errors;
    err->overflow = sc_overflow;
    return 0;
}

const cb_can_backend_t cb_can_socketcan = {
    .name = "socketcan",
    .open = socketcan_open,
    .close = socketcan_close,
    .send = socketcan_send,
    .recv = socketcan_recv,
    .get_errors = socketcan_get_errors,
};
