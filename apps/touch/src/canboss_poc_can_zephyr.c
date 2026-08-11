/**
 * canboss_poc_can_zephyr.c
 *
 * Zephyr-Backend der PoC-Hallenlichtsteuerung.
 *
 *  - TX: Raw-Frame direkt ueber den Chosen-CAN (zephyr,canbus),
 *    absichtlich K_NO_WAIT. Der gemeinsame can_if-Pfad blockiert bis
 *    100 ms auf ACK - ohne zweiten Knoten (oder bei offenem Bus) legt
 *    das den LVGL-Thread lahm und der PoC-Screen „haengt“.
 *  - RX: weiterhin ueber den CANopen-RX-Hook (canboss_poc_can_rx)
 */

#include "canboss_poc.h"
#include "canboss_poc_gen.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>

bool
canboss_poc_can_tx(const uint8_t payload[6]) {
	const struct device *can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
	struct can_frame zf;
	int ret;

	if (!device_is_ready(can_dev)) {
		return false;
	}

	memset(&zf, 0, sizeof(zf));
	zf.id = CANBOSS_POC_TX_ID;
	zf.dlc = 6;
	memcpy(zf.data, payload, 6);

	/* Nie den UI-Thread blockieren: volles TX-FIFO = Frame verwerfen. */
	ret = can_send(can_dev, &zf, K_NO_WAIT, NULL, NULL);
	return ret == 0;
}
