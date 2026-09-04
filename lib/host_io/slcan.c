/**
 * slcan.c - SLCAN-Bruecke im NDJSON-Strom, siehe
 * slcan.h. Eigenstaendige Implementierung des
 * Lawicel-Protokolls (CANUSB); Kommandosatz und Antwortkonventionen
 * wie bei den ueblichen slcan-Firmwares.
 *
 * Threading wie beim PDO-Monitor in ndjson.c: der Raw-RX-Hook
 * (CAN-RX-Thread) legt Frames in einen Ringpuffer, die Ausgabe
 * laeuft komplett im poll-Thread.
 */

#include "slcan.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>

#include <stdio.h>
#include <string.h>

#include "co_node.h"
#include "osal.h"

#define SLCAN_VERSION "V0101" /* Vhhss: Hardware 01, Software 01 */
#define SLCAN_SERIAL  "NCB01"

struct slcan_rec {
    uint16_t id;
    uint8_t dlc;
    uint8_t rtr;
    uint8_t data[8];
    uint16_t t_ms; /* Lawicel-Timestamp: ms, Wrap bei 60000 */
};

RING_BUF_DECLARE(slcan_rb, 32 * sizeof(struct slcan_rec));

static void (*slcan_out)(const char* buf, size_t len);
static bool slcan_open;
static bool slcan_timestamp;
static uint32_t slcan_overrun; /* verworfene Frames seit letztem 'F' */

static void
reply(const char* s) {
    if (slcan_out != NULL) {
        slcan_out(s, strlen(s));
    }
}

static void
reply_ok(void) {
    reply("\r");
}

static void
reply_err(void) {
    /* BEL laut Protokoll ohne CR; das CR dahinter schadet echten
     * SLCAN-Hosts nicht (leere Zeile) und macht den Fehler fuer
     * zeilenbasierte Clients (Webapp) sichtbar. */
    reply("\a\r");
}

static int
hex_val(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

/* n Hex-Ziffern ab s einlesen; -1 bei Nicht-Hex */
static long
hex_field(const char* s, int n) {
    long v = 0;

    for (int i = 0; i < n; i++) {
        int d = hex_val(s[i]);

        if (d < 0) {
            return -1;
        }
        v = (v << 4) | d;
    }
    return v;
}

/* "tiiildd.." / "riiil": Frame parsen und senden */
static void
tx_frame(const char* line, size_t len, bool rtr) {
    long id, dlc;
    uint8_t data[8];

    if (len < 5) {
        reply_err();
        return;
    }
    id = hex_field(&line[1], 3);
    dlc = hex_field(&line[4], 1);
    if (id < 0 || id > 0x7FF || dlc < 0 || dlc > 8) {
        reply_err();
        return;
    }
    if (rtr) {
        /* RTR traegt nur die DLC, keine Datenbytes */
        if (len != 5) {
            reply_err();
            return;
        }
    } else {
        if (len != 5 + 2 * (size_t)dlc) {
            reply_err();
            return;
        }
        for (int i = 0; i < dlc; i++) {
            long b = hex_field(&line[5 + 2 * i], 2);

            if (b < 0) {
                reply_err();
                return;
            }
            data[i] = (uint8_t)b;
        }
    }

    if (cb_co_raw_send((uint16_t)id, data, (uint8_t)dlc, rtr) != 0) {
        reply_err();
        return;
    }
    reply("z\r");
}

void
slcan_init(void (*out)(const char* buf, size_t len)) {
    slcan_out = out;
}

void
slcan_line(const char* line, size_t len) {
    if (len == 0) {
        return;
    }

    switch (line[0]) {
    case 't':
        tx_frame(line, len, false);
        break;
    case 'r':
        tx_frame(line, len, true);
        break;

    case 'T':
    case 'R':
        /* Extended-Frames: can_if ist bewusst 11-Bit-only */
        reply_err();
        break;

    case 'O': /* Kanal auf: RX-Weiterleitung an */
    case 'L': /* Listen-only: wir hoeren ohnehin nur mit; der Bus
               * bleibt im Normalmodus (CANopen sendet parallel) */
        if (len != 1 || !cb_co_connected()) {
            reply_err();
            break;
        }
        slcan_open = true;
        reply_ok();
        break;

    case 'C':
        if (len != 1) {
            reply_err();
            break;
        }
        slcan_open = false;
        reply_ok();
        break;

    case 'Z': /* "Z0"/"Z1", nacktes "Z" toggelt */
        if (len == 1) {
            slcan_timestamp = !slcan_timestamp;
        } else if (len == 2 && (line[1] == '0' || line[1] == '1')) {
            slcan_timestamp = (line[1] == '1');
        } else {
            reply_err();
            break;
        }
        reply_ok();
        break;

    case 'V':
    case 'v':
        reply(SLCAN_VERSION "\r");
        break;

    case 'N':
        reply(SLCAN_SERIAL "\r");
        break;

    case 'F': { /* Statusflags; Bit 3 = RX-Ueberlauf seit letztem F */
        char buf[8];

        (void)snprintf(buf, sizeof(buf), "F%02X\r", (slcan_overrun > 0) ? 0x08 : 0x00);
        slcan_overrun = 0;
        reply(buf);
        break;
    }

    /* Init-Sequenzen von slcand & Co. nicht scheitern lassen:
     * Bitrate (S/s) und Filter (M/m) gehoeren dem CANopen-Stack bzw.
     * dem Devicetree, UART-Optionen (U/X/A/W/Q/P/p) gibt es hier
     * nicht - annehmen und ignorieren. */
    case 'S':
    case 's':
    case 'M':
    case 'm':
    case 'U':
    case 'X':
    case 'A':
    case 'W':
    case 'Q':
    case 'P':
    case 'p':
        reply_ok();
        break;

    default:
        reply_err();
        break;
    }
}

void
slcan_rx(uint16_t ident, const uint8_t* data, uint8_t dlc, bool rtr) {
    if (!slcan_open) {
        return;
    }

    struct slcan_rec rec = {
        .id = ident,
        .dlc = dlc > 8 ? 8 : dlc,
        .rtr = rtr ? 1 : 0,
        .t_ms = (uint16_t)((cb_now_us() / 1000u) % 60000u),
    };

    if (!rtr) {
        memcpy(rec.data, data, rec.dlc);
    }
    if (ring_buf_space_get(&slcan_rb) < sizeof(rec)) {
        slcan_overrun++;
        return;
    }
    ring_buf_put(&slcan_rb, (const uint8_t*)&rec, sizeof(rec));
}

void
slcan_pump(void) {
    struct slcan_rec rec;

    while (ring_buf_get(&slcan_rb, (uint8_t*)&rec, sizeof(rec)) == sizeof(rec)) {
        char buf[32]; /* r/t + 3 + 1 + 16 Daten + 4 Timestamp + CR + NUL */
        int n = snprintf(buf, sizeof(buf), "%c%03X%1X", rec.rtr ? 'r' : 't', rec.id, rec.dlc);

        if (!rec.rtr) {
            for (int i = 0; i < rec.dlc; i++) {
                n += snprintf(&buf[n], sizeof(buf) - (size_t)n, "%02X", rec.data[i]);
            }
        }
        if (slcan_timestamp) {
            n += snprintf(&buf[n], sizeof(buf) - (size_t)n, "%04X", rec.t_ms);
        }
        buf[n++] = '\r';
        if (slcan_out != NULL) {
            slcan_out(buf, (size_t)n);
        }
    }
}
