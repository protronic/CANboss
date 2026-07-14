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
#include <linux/can/raw.h>

static int can_fd = -1;

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

    return 0;
}

static void
socketcan_close(void) {
    if (can_fd >= 0) {
        close(can_fd);
        can_fd = -1;
    }
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
    if (n < (ssize_t)sizeof(cf) || (cf.can_id & (CAN_EFF_FLAG | CAN_ERR_FLAG))) {
        return 0; /* nur klassische 11-Bit-Frames */
    }

    frame->id = (uint16_t)(cf.can_id & CAN_SFF_MASK);
    frame->rtr = (cf.can_id & CAN_RTR_FLAG) != 0;
    frame->dlc = cf.can_dlc > 8 ? 8 : cf.can_dlc;
    memcpy(frame->data, cf.data, frame->dlc);
    return 1;
}

const cb_can_backend_t cb_can_socketcan = {
    .name = "socketcan",
    .open = socketcan_open,
    .close = socketcan_close,
    .send = socketcan_send,
    .recv = socketcan_recv,
};
