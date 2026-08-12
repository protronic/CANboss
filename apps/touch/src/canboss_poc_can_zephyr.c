/**
 * canboss_poc_can_zephyr.c
 *
 * Zephyr-Backend der PoC-Hallenlichtsteuerung.
 *
 *  - TX: Raw-Frame ueber Chosen-CAN (zephyr,canbus). WICHTIG: callback
 *    darf NICHT NULL sein - z_impl_can_send() wartet sonst mit
 *    k_sem_take(..., K_FOREVER) auf TX-Complete/ACK. Ohne zweiten Knoten
 *    haengt der LVGL-Thread Sekunden (PoC laesst sich nicht verlassen).
 *  - RX: weiterhin ueber den CANopen-RX-Hook (canboss_poc_can_rx)
 */

#include "canboss_poc.h"
#include "canboss_poc_gen.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

static atomic_t poc_tx_fail_count;

static void
poc_can_tx_done(const struct device *dev, int error, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	if (error != 0) {
		atomic_inc(&poc_tx_fail_count);
	}
}

bool
canboss_poc_can_tx_had_error(void)
{
	return atomic_set(&poc_tx_fail_count, 0) != 0;
}

bool
canboss_poc_can_tx(const uint8_t payload[6])
{
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

	/* K_NO_WAIT = Mailbox; Callback = kein Blockieren auf ACK. */
	ret = can_send(can_dev, &zf, K_NO_WAIT, poc_can_tx_done, NULL);
	return ret == 0;
}
