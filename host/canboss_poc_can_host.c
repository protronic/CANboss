/**
 * canboss_poc_can_host.c
 *
 * SocketCAN-Backend der PoC-Hallenlichtsteuerung fuer den Linux-Host-Build
 * (Gegenstueck zu App/canboss_poc_can_stm32.c; gleiche Nutzung wie der
 * oxivgl-host-PoC: vcan0 + candump/cansend zum Testen).
 *
 *  - TX: write() eines classic-CAN-Frames auf CANBOSS_POC_TX_ID
 *  - RX: Lese-Thread reicht jeden Frame an canboss_poc_can_rx() weiter
 *    (die PoC-UI filtert selbst auf die minp-ID)
 */

#include "canboss_poc.h"
#include "canboss_poc_gen.h"
#include "canboss_host.h"

#include <errno.h>
#include <net/if.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <linux/can.h>
#include <linux/can/raw.h>

static int can_fd = -1;
static pthread_t rx_thread;

static void*
host_can_rx_loop(void* arg) {
    (void)arg;
    struct can_frame frame;

    for (;;) {
        ssize_t n = read(can_fd, &frame, sizeof(frame));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("canboss_poc: CAN read");
            break;
        }
        if (n < (ssize_t)sizeof(frame) || (frame.can_id & (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG))) {
            continue; /* nur klassische 11-Bit-Datenframes */
        }
        canboss_poc_can_rx((uint16_t)(frame.can_id & CAN_SFF_MASK), frame.data, frame.can_dlc);
    }
    return NULL;
}

int
canboss_poc_can_host_init(const char* ifname) {
    struct ifreq ifr;
    struct sockaddr_can addr;

    can_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (can_fd < 0) {
        perror("canboss_poc: CAN socket");
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifname);
    if (ioctl(can_fd, SIOCGIFINDEX, &ifr) < 0) {
        perror("canboss_poc: CAN interface");
        close(can_fd);
        can_fd = -1;
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(can_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("canboss_poc: CAN bind");
        close(can_fd);
        can_fd = -1;
        return -1;
    }

    if (pthread_create(&rx_thread, NULL, host_can_rx_loop, NULL) != 0) {
        perror("canboss_poc: CAN rx thread");
        close(can_fd);
        can_fd = -1;
        return -1;
    }
    return 0;
}

bool
canboss_poc_can_tx(const uint8_t payload[6]) {
    if (can_fd < 0) {
        return false;
    }

    struct can_frame frame;
    memset(&frame, 0, sizeof(frame));
    frame.can_id = CANBOSS_POC_TX_ID;
    frame.can_dlc = 6;
    memcpy(frame.data, payload, 6);

    return write(can_fd, &frame, sizeof(frame)) == (ssize_t)sizeof(frame);
}
