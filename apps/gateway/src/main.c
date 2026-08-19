/**
 * CANboss-Gateway: NDJSON-Interface (lib/jsonapi) fuer Webapps.
 *
 * Transport ist eine UART, ausgewaehlt ueber das chosen
 * "canboss,jsonapi-uart" (Fallback: zephyr,console):
 *   - native_sim:     stdin/stdout des Prozesses (Test per Pipe/socat)
 *   - stm32h573i_dk:  USB-CDC-ACM am USB-Stecker des Boards, im Browser
 *                     direkt per WebSerial erreichbar (src/usb_cdc.c);
 *                     die ST-Link-VCP bleibt frei fuer Konsole/Logs.
 *                     Rueckfall auf die VCP: overlays/stlink_vcp.*
 *
 * CANopen laeuft mit dem canboss_master-OD (Default-Node 126) auf dem
 * chosen zephyr,canbus; das CANopenNode-Gateway (CiA 309-3) wird ueber
 * CNT_GTWA=1 aktiviert und von jsonapi als {"gtwa": ...} angesprochen.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>

/* Statusmeldungen auf der Zephyr-Konsole (stm32h573i_dk: ST-Link-VCP,
 * boards/stm32h573i_dk.conf). Auf native_sim ist CONFIG_LOG=n - dort
 * gehoert stdout dem NDJSON-Strom und LOG_* compiliert zu nichts. */
LOG_MODULE_REGISTER(canboss_gw, LOG_LEVEL_INF);

#include <stdio.h>
#include <string.h>

#include "co_node.h"
#include "canboss_master.h" /* nach co_node.h (CANopenNode-Typen) */
#include "jsonapi.h"
#include "usb_cdc.h"

#ifdef CANBOSS_BERRY
#include "canboss_berry.h"
#endif

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

#ifdef CANBOSS_BERRY
/* {"repl": ...}: canboss_berry_exec() im poll-Kontext; die
 * Berry-Ausgabe (be_writebuffer -> lib/berry_od/berry_port.c -> Senke)
 * geht an jsonapi_repl_out und damit als {"repl":[...]} raus. */
static void
berry_sink(void* user, const char* buf, size_t len) {
    (void)user;
    jsonapi_repl_out(buf, len);
}

static int
repl_exec(void* user, const char* code) {
    (void)user;
    return canboss_berry_exec(code);
}
#endif

int
main(void) {
    const cb_can_backend_t* backend = cb_can_backend_find("zephyr");

    if (!device_is_ready(gw_uart)) {
        return -1;
    }

    LOG_INF("CANboss-Gateway startet (Node-ID %d)", CONFIG_CANBOSS_GW_NODE_ID);

    /* USB-Device hochfahren, bevor CANopen startet: die Enumeration
     * laeuft dann parallel zur Bus-Initialisierung. Ohne
     * CONFIG_CANBOSS_GW_USB (native_sim, VCP-Variante) ein No-op. */
    int usb_err = canboss_usb_init();

    if (usb_err != 0) {
        LOG_ERR("USB-CDC-Init fehlgeschlagen (%d) - NDJSON-Transport tot", usb_err);
    }

    jsonapi_sink_t sink = {.write = sink_write, .user = NULL};

    (void)jsonapi_init(&sink, &jsonapi_fw_ram);

#ifdef CANBOSS_BERRY
    canboss_berry_init(canboss_master);
    cb_berry_set_sink(berry_sink, NULL);
    jsonapi_set_repl(repl_exec, NULL);
#endif

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
        LOG_ERR("CANopen offline: %s", cb_co_error());
        /* weiterlaufen: ping/info/fw-Upload gehen auch offline */
    } else {
        LOG_INF("CANopen laeuft (gtwa + SLCAN bereit)");
    }

    for (;;) {
        unsigned char c;

        /* Webapp hat den Port gerade geoeffnet: eine Zeile abschliessen,
         * die noch im TX-Puffer stand (etwa die Offline-Meldung von
         * oben). Sonst setzt die Webapp auf einem Fragment auf und
         * verwirft die erste echte Antwort als Parse-Fehler. */
        if (canboss_usb_take_attach()) {
            sink_write(NULL, "\n", 1);
        }

        while (uart_poll_in(gw_uart, &c) == 0) {
            jsonapi_input(&c, 1);
        }
        jsonapi_poll();
        k_sleep(K_MSEC(2));
    }
    return 0;
}
