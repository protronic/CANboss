/**
 * canboss_poc_can_zephyr.c
 *
 * Zephyr-Backend der PoC-Hallenlichtsteuerung (Gegenstueck zu
 * App/canboss_poc_can_stm32.c bzw. host/canboss_poc_can_host.c).
 *
 *  - TX: Raw-Frame ueber das can_if-Backend (gleicher Bus wie CANopen)
 *  - RX: der RX-Thread in co_zephyr.c reicht jeden Frame an
 *    canboss_poc_can_rx() weiter (die PoC-UI filtert selbst)
 */

#include "canboss_poc.h"
#include "canboss_poc_gen.h"

#include "can_if.h"

#include <string.h>

bool
canboss_poc_can_tx(const uint8_t payload[6]) {
    const cb_can_backend_t* backend = cb_can_backend_find("zephyr");
    cb_can_frame_t frame;

    if (backend == NULL) {
        return false;
    }

    frame.id = CANBOSS_POC_TX_ID;
    frame.dlc = 6;
    frame.rtr = false;
    memset(frame.data, 0, sizeof(frame.data));
    memcpy(frame.data, payload, 6);

    return backend->send(&frame) == 0;
}
