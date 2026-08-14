/**
 * CANboss-Gateway: NDJSON-Interface (lib/jsonapi) fuer Webapps.
 *
 * Transport ist eine UART - auf native_sim stdin/stdout des Prozesses
 * (Test per Pipe/socat), auf stm32h573i_dk die ST-Link-VCP, die im
 * Browser direkt per WebSerial erreichbar ist. Ein anderes Geraet
 * (z.B. CDC-ACM-Instanz) waehlt das chosen "canboss,jsonapi-uart".
 *
 * CANopen laeuft mit dem canboss_master-OD (Default-Node 126) auf dem
 * chosen zephyr,canbus; das CANopenNode-Gateway (CiA 309-3) wird ueber
 * CNT_GTWA=1 aktiviert und von jsonapi als {"gtwa": ...} angesprochen.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>

#include <stdio.h>
#include <string.h>

#include "co_node.h"
#include "canboss_master.h" /* nach co_node.h (CANopenNode-Typen) */
#include "jsonapi.h"

#if DT_HAS_CHOSEN(canboss_jsonapi_uart)
#define GW_UART_NODE DT_CHOSEN(canboss_jsonapi_uart)
#else
#define GW_UART_NODE DT_CHOSEN(zephyr_console)
#endif

static const struct device* const gw_uart = DEVICE_DT_GET(GW_UART_NODE);

static void
sink_write(void* user, const void* buf, size_t len) {
    const uint8_t* p = buf;

    (void)user;
    for (size_t i = 0; i < len; i++) {
        uart_poll_out(gw_uart, p[i]);
    }
}

int
main(void) {
    const cb_can_backend_t* backend = cb_can_backend_find("zephyr");

    if (!device_is_ready(gw_uart)) {
        return -1;
    }

    jsonapi_sink_t sink = {.write = sink_write, .user = NULL};

    (void)jsonapi_init(&sink, &jsonapi_fw_ram);

    /* OD-Konfiguration des Masters (muss den Stack ueberleben) */
    static CO_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    canboss_master_INIT_CONFIG(cfg);
    cfg.CNT_GTWA = 1; /* Gateway-ASCII aktivieren (co_node/jsonapi) */

    if (cb_co_start(canboss_master, &cfg, backend, "zephyr,canbus", 0, CONFIG_CANBOSS_GW_NODE_ID) != 0) {
        char msg[192];
        int n = snprintf(msg, sizeof(msg), "{\"err\":\"CANopen offline: %s\"}\n", cb_co_error());

        if (n > 0) {
            sink_write(NULL, msg, (size_t)n);
        }
        /* weiterlaufen: ping/info/fw-Upload gehen auch offline */
    }

    for (;;) {
        unsigned char c;

        while (uart_poll_in(gw_uart, &c) == 0) {
            jsonapi_input(&c, 1);
        }
        jsonapi_poll();
        k_sleep(K_MSEC(2));
    }
    return 0;
}
