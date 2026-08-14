/**
 * jsonapi.c - NDJSON-Interface fuer Webapps, siehe jsonapi.h
 *
 * Zephyr-only (Ringpuffer, Spinlock, k_sleep); genutzt von
 * apps/gateway (USB-CDC/Konsole) und perspektivisch canBLEberry (BLE).
 *
 * Datenfluesse:
 *   Transport-RX (beliebiger Kontext) -> in_rb  -> poll: Zeile -> Dispatch
 *   GTWA-Antworten (Mainline-Thread)  -> gtwa_rb-> poll: {"gtwa":[...]}
 *   Raw-RX-Hook (CAN-RX-Thread)       -> pdo_rb -> poll: {"pdo":{...}}
 *   fw send: blockiert in poll, Fortschritt als {"fw":{"prog":...}}
 */

#include "jsonapi.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "co_node.h"
#include "osal.h"

#include "jsonapi_json.h"

#if ((CO_CONFIG_GTW)&CO_CONFIG_GTW_ASCII) == 0
#error "lib/jsonapi braucht CO_CONFIG_GTW_ASCII (per CO_DRIVER_CUSTOM aktivieren, siehe apps/gateway)"
#endif

#define CB_JSONAPI_LINE_MAX     3584 /* laengste Requestzeile (fw data, ~3 KiB Base64) */
#define CB_JSONAPI_GTWA_CMD_MAX 200  /* eine 309-3-Kommandozeile */
#define CB_JSONAPI_IN_RB_SIZE   4608
#define CB_JSONAPI_GTWA_RB_SIZE 1024
#define CB_JSONAPI_PDO_RB_SIZE  (32 * sizeof(struct pdo_rec))
#define CB_JSONAPI_MON_MAX      16
#define CB_JSONAPI_MON_RATE_MS  100 /* Default-Mindestabstand je COB */

#ifndef CB_JSONAPI_FW_SDO_TIMEOUT_MS
#define CB_JSONAPI_FW_SDO_TIMEOUT_MS 2000
#endif
#ifndef CB_JSONAPI_FW_INDEX_DEFAULT
#define CB_JSONAPI_FW_INDEX_DEFAULT 0x1F50 /* CiA 302 Program data */
#endif
#ifndef CB_JSONAPI_FW_SUB_DEFAULT
#define CB_JSONAPI_FW_SUB_DEFAULT 1
#endif

struct pdo_rec {
    uint16_t cob;
    uint8_t dlc;
    uint8_t data[8];
    uint32_t t_ms;
};

static jsonapi_sink_t out_sink;
static const jsonapi_fw_ops_t* fw;

RING_BUF_DECLARE(in_rb, CB_JSONAPI_IN_RB_SIZE);
RING_BUF_DECLARE(gtwa_rb, CB_JSONAPI_GTWA_RB_SIZE);
RING_BUF_DECLARE(pdo_rb, CB_JSONAPI_PDO_RB_SIZE);

static char req_line[CB_JSONAPI_LINE_MAX];
static size_t req_len;
static bool req_overflow;

static char gtwa_line[256];
static size_t gtwa_len;

/* PDO-Monitor-Tabelle (Schreiber: poll; Leser: CAN-RX-Thread) */
static struct k_spinlock mon_lock;
static struct {
    uint16_t cob;
    uint32_t last_ms;
} mon_tab[CB_JSONAPI_MON_MAX];
static int mon_count;
static uint32_t mon_rate_ms = CB_JSONAPI_MON_RATE_MS;
static uint32_t mon_dropped;

/* fw-Upload-Zustand (nur poll-Thread) */
static bool fw_uploading;
static size_t fw_expect, fw_got;
static char fw_name[JSONAPI_FW_NAME_MAX + 1];

/* ------------------------------------------------------------------ */
/* Ausgabe                                                             */

static void
out_raw(const char* buf, size_t len) {
    if (out_sink.write != NULL) {
        out_sink.write(out_sink.user, buf, len);
    }
}

static void __attribute__((format(printf, 1, 2)))
emitf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    if ((size_t)n > sizeof(buf) - 2) {
        n = sizeof(buf) - 2;
    }
    buf[n++] = '\n';
    out_raw(buf, (size_t)n);
}

static void
emit_err(const char* msg) {
    char q[160];

    cj_quote(msg, strlen(msg), q, sizeof(q));
    emitf("{\"err\":%s}", q);
}

/* ------------------------------------------------------------------ */
/* Base64 (RFC 4648, ohne Zeilenumbrueche)                             */

static int
b64_val(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

static int
b64_decode(const char* in, size_t in_len, uint8_t* out, size_t out_size, size_t* out_len) {
    size_t n = 0;
    int acc = 0, bits = 0;

    for (size_t i = 0; i < in_len; i++) {
        char c = in[i];

        if (c == '=') {
            break;
        }
        int v = b64_val(c);

        if (v < 0) {
            return -EINVAL;
        }
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n >= out_size) {
                return -ENOSPC;
            }
            out[n++] = (uint8_t)((acc >> bits) & 0xFF);
        }
    }
    *out_len = n;
    return 0;
}

/* ------------------------------------------------------------------ */
/* GTWA-Bruecke                                                        */

/* Antwort-Senke des Gateways; laeuft im CANopen-Mainline-Thread.
 * Teilabnahme (Ring voll) haelt die Gateway-Ausgabe an (respHold). */
static size_t
gtwa_sink(void* user, const char* buf, size_t len) {
    (void)user;
    return ring_buf_put(&gtwa_rb, (const uint8_t*)buf, (uint32_t)len);
}

/* Eine Kommandozeile (ohne '\n') ins Gateway schieben */
static void
gtwa_send_line(const char* cmd, size_t len) {
    const char* p = cmd;
    size_t rest = len;
    int tries = 500; /* max ~500 ms auf Fifo-Platz warten */

    while (rest > 0 && tries-- > 0) {
        int n = cb_co_gtwa_write(p, rest);

        if (n < 0) {
            emit_err("gtwa: Gateway laeuft nicht (CANopen offline oder CNT_GTWA=0)");
            return;
        }
        p += n;
        rest -= (size_t)n;
        if (rest > 0) {
            k_sleep(K_MSEC(1));
        }
    }
    if (rest > 0 || cb_co_gtwa_write("\n", 1) != 1) {
        emit_err("gtwa: Kommando-Fifo voll");
    }
}

/* GTWA-Antwortbytes zu Zeilen buendeln und als Event ausgeben */
static void
gtwa_pump(void) {
    uint8_t c;

    while (ring_buf_get(&gtwa_rb, &c, 1) == 1) {
        if (c == '\r') {
            continue;
        }
        if (c != '\n') {
            if (gtwa_len < sizeof(gtwa_line) - 1) {
                gtwa_line[gtwa_len++] = (char)c;
            }
            continue;
        }
        if (gtwa_len == 0) {
            continue;
        }

        char q[420];

        cj_quote(gtwa_line, gtwa_len, q, sizeof(q));
        emitf("{\"gtwa\":[%s]}", q);
        gtwa_len = 0;
    }
}

/* ------------------------------------------------------------------ */
/* PDO-Monitor                                                         */

/* Raw-RX-Hook: laeuft im CAN-RX-Thread, muss kurz bleiben. */
static void
mon_rx_hook(uint16_t ident, const uint8_t* data, uint8_t dlc) {
    uint32_t now_ms = (uint32_t)(cb_now_us() / 1000u);
    bool take = false;

    K_SPINLOCK(&mon_lock) {
        for (int i = 0; i < mon_count; i++) {
            if (mon_tab[i].cob == ident) {
                if (mon_rate_ms == 0 || now_ms - mon_tab[i].last_ms >= mon_rate_ms) {
                    mon_tab[i].last_ms = now_ms;
                    take = true;
                }
                break;
            }
        }
    }
    if (!take) {
        return;
    }

    struct pdo_rec rec = {.cob = ident, .dlc = dlc > 8 ? 8 : dlc, .t_ms = now_ms};

    memcpy(rec.data, data, rec.dlc);
    if (ring_buf_space_get(&pdo_rb) < sizeof(rec)) {
        mon_dropped++;
        return;
    }
    ring_buf_put(&pdo_rb, (const uint8_t*)&rec, sizeof(rec));
}

static void
mon_pump(void) {
    struct pdo_rec rec;

    while (ring_buf_get(&pdo_rb, (uint8_t*)&rec, sizeof(rec)) == sizeof(rec)) {
        char hex[17];

        for (int i = 0; i < rec.dlc; i++) {
            snprintf(&hex[2 * i], 3, "%02x", rec.data[i]);
        }
        hex[2 * rec.dlc] = '\0';
        emitf("{\"pdo\":{\"cob\":%u,\"d\":\"%s\",\"t\":%u}}", rec.cob, hex, rec.t_ms);
    }
    if (mon_dropped > 0) {
        emitf("{\"mon\":{\"dropped\":%u}}", mon_dropped);
        mon_dropped = 0;
    }
}

static void
mon_reply_active(void) {
    char list[128];
    size_t n = 0;

    K_SPINLOCK(&mon_lock) {
        for (int i = 0; i < mon_count && n < sizeof(list) - 8; i++) {
            n += (size_t)snprintf(&list[n], sizeof(list) - n, "%s%u", (i > 0) ? "," : "", mon_tab[i].cob);
        }
    }
    list[n] = '\0';
    emitf("{\"mon\":{\"ok\":true,\"active\":[%s],\"rate\":%u}}", list, mon_rate_ms);
}

static void
mon_handle(cj_t mon) {
    cj_t v;
    int64_t num;

    if (cj_obj_get(mon, "clear", &v)) {
        bool b;

        if (cj_as_bool(v, &b) && b) {
            K_SPINLOCK(&mon_lock) {
                mon_count = 0;
            }
        }
    }
    if (cj_obj_get(mon, "rate", &v) && cj_as_i64(v, &num) && num >= 0) {
        mon_rate_ms = (uint32_t)num;
    }
    if (cj_obj_get(mon, "del", &v) && cj_is_arr(v)) {
        size_t it = 0;
        cj_t e;

        while (cj_arr_next(v, &it, &e)) {
            if (!cj_as_i64(e, &num)) {
                continue;
            }
            K_SPINLOCK(&mon_lock) {
                for (int i = 0; i < mon_count; i++) {
                    if (mon_tab[i].cob == (uint16_t)num) {
                        mon_tab[i] = mon_tab[mon_count - 1];
                        mon_count--;
                        break;
                    }
                }
            }
        }
    }
    if (cj_obj_get(mon, "add", &v) && cj_is_arr(v)) {
        size_t it = 0;
        cj_t e;

        while (cj_arr_next(v, &it, &e)) {
            if (!cj_as_i64(e, &num) || num <= 0 || num > 0x7FF) {
                continue;
            }
            K_SPINLOCK(&mon_lock) {
                bool known = false;

                for (int i = 0; i < mon_count; i++) {
                    known = known || (mon_tab[i].cob == (uint16_t)num);
                }
                if (!known && mon_count < CB_JSONAPI_MON_MAX) {
                    mon_tab[mon_count].cob = (uint16_t)num;
                    mon_tab[mon_count].last_ms = 0;
                    mon_count++;
                }
            }
        }
    }
    mon_reply_active();
}

/* ------------------------------------------------------------------ */
/* fw-Kommandos                                                        */

struct fw_send_ctx {
    int slot;
    int node;
    size_t off;
    size_t size;
    size_t last_emit;
};

static int
fw_send_read(void* ctx, uint8_t* buf, size_t max) {
    struct fw_send_ctx* c = ctx;
    size_t n = (c->size - c->off) < max ? (c->size - c->off) : max;

    if (n == 0) {
        return 0;
    }
    if (fw->read(c->slot, c->off, buf, n) != 0) {
        return -1;
    }
    c->off += n;
    return (int)n;
}

static void
fw_send_progress(void* ctx, size_t sent, size_t total) {
    struct fw_send_ctx* c = ctx;

    if (sent - c->last_emit >= 4096u || sent == total) {
        c->last_emit = sent;
        emitf("{\"fw\":{\"prog\":[%u,%u],\"slot\":%d,\"node\":%d}}", (unsigned int)sent, (unsigned int)total, c->slot,
              c->node);
    }
}

static void
fw_handle(cj_t obj) {
    char op[16] = "";
    cj_t v;
    int64_t num;

    if (fw == NULL) {
        emit_err("fw: kein Speicher-Backend konfiguriert");
        return;
    }

    /* Datenchunks zuerst (haeufigster Fall) */
    if (cj_obj_get(obj, "b64", &v) || (cj_obj_get(obj, "op", &v) && cj_as_str(v, op, sizeof(op)) && strcmp(op, "data") == 0)) {
        static uint8_t bin[2816];
        size_t bin_len = 0;
        cj_t b64;

        if (!fw_uploading) {
            emit_err("fw: kein Upload aktiv (op begin fehlt)");
            return;
        }
        if (!cj_obj_get(obj, "b64", &b64) || !cj_is_str(b64)) {
            emit_err("fw data: b64 fehlt");
            return;
        }
        if (b64_decode(b64.s + 1, b64.len - 2, bin, sizeof(bin), &bin_len) != 0) {
            emit_err("fw data: Base64 ungueltig/zu lang");
            return;
        }
        if (fw->write(bin, bin_len) != 0) {
            fw->write_abort();
            fw_uploading = false;
            emit_err("fw data: Schreiben fehlgeschlagen");
            return;
        }
        fw_got += bin_len;
        emitf("{\"fw\":{\"ack\":%u}}", (unsigned int)fw_got);
        return;
    }

    if (!cj_obj_get(obj, "op", &v) || cj_as_str(v, op, sizeof(op)) == (size_t)-1) {
        emit_err("fw: op fehlt");
        return;
    }

    if (strcmp(op, "begin") == 0) {
        int64_t slot = 0, size = 0;

        if (!cj_obj_get(obj, "slot", &v) || !cj_as_i64(v, &slot) || !cj_obj_get(obj, "size", &v)
            || !cj_as_i64(v, &size) || size <= 0 || (size_t)size > fw->slot_capacity()) {
            emitf("{\"err\":\"fw begin: slot/size ungueltig (max %u)\"}", (unsigned int)fw->slot_capacity());
            return;
        }
        fw_name[0] = '\0';
        if (cj_obj_get(obj, "name", &v)) {
            (void)cj_as_str(v, fw_name, sizeof(fw_name));
        }
        if (fw->write_begin((int)slot) != 0) {
            emit_err("fw begin: Slot ungueltig/Erase fehlgeschlagen");
            return;
        }
        fw_uploading = true;
        fw_expect = (size_t)size;
        fw_got = 0;
        emitf("{\"fw\":{\"ok\":\"begin\",\"slot\":%d,\"size\":%u}}", (int)slot, (unsigned int)size);

    } else if (strcmp(op, "end") == 0) {
        char crc_s[12] = "";
        uint32_t crc_actual = 0;

        if (!fw_uploading) {
            emit_err("fw end: kein Upload aktiv");
            return;
        }
        fw_uploading = false;
        if (!cj_obj_get(obj, "crc32", &v) || cj_as_str(v, crc_s, sizeof(crc_s)) == (size_t)-1) {
            fw->write_abort();
            emit_err("fw end: crc32 fehlt");
            return;
        }
        if (fw_got != fw_expect) {
            fw->write_abort();
            emitf("{\"err\":\"fw end: %u von %u Bytes empfangen\"}", (unsigned int)fw_got, (unsigned int)fw_expect);
            return;
        }

        uint32_t crc_expected = (uint32_t)strtoul(crc_s, NULL, 16);
        int rc = fw->write_end(fw_name, crc_expected, &crc_actual);

        if (rc != 0) {
            emitf("{\"err\":\"fw end: CRC/Abschluss fehlgeschlagen (erwartet %08x, gelesen %08x)\"}",
                  (unsigned int)crc_expected, (unsigned int)crc_actual);
        } else {
            emitf("{\"fw\":{\"ok\":\"end\",\"crc32\":\"%08x\"}}", (unsigned int)crc_actual);
        }

    } else if (strcmp(op, "abort") == 0) {
        fw->write_abort();
        fw_uploading = false;
        emitf("{\"fw\":{\"ok\":\"abort\"}}");

    } else if (strcmp(op, "list") == 0) {
        /* Zeile direkt bauen: kann laenger werden als der emitf-Puffer */
        static char line_buf[1152];
        size_t n = (size_t)snprintf(line_buf, sizeof(line_buf), "{\"fw\":{\"slots\":[");

        for (int i = 0; i < fw->slot_count(); i++) {
            size_t size;
            uint32_t crc;
            char name[JSONAPI_FW_NAME_MAX + 1];
            char qname[100];
            char item[176];
            size_t item_len;

            if (fw->slot_info(i, &size, &crc, name, sizeof(name)) == 0) {
                cj_quote(name, strlen(name), qname, sizeof(qname));
                item_len = (size_t)snprintf(item, sizeof(item),
                                            "%s{\"slot\":%d,\"size\":%u,\"crc32\":\"%08x\",\"name\":%s}",
                                            (i > 0) ? "," : "", i, (unsigned int)size, (unsigned int)crc, qname);
            } else {
                item_len = (size_t)snprintf(item, sizeof(item), "%s{\"slot\":%d}", (i > 0) ? "," : "", i);
            }
            if (n + item_len < sizeof(line_buf) - 32) {
                memcpy(&line_buf[n], item, item_len);
                n += item_len;
            }
        }
        n += (size_t)snprintf(&line_buf[n], sizeof(line_buf) - n, "],\"slotcap\":%u}}\n",
                              (unsigned int)fw->slot_capacity());
        out_raw(line_buf, n);

    } else if (strcmp(op, "erase") == 0) {
        if (!cj_obj_get(obj, "slot", &v) || !cj_as_i64(v, &num) || fw->erase((int)num) != 0) {
            emit_err("fw erase: Slot ungueltig");
            return;
        }
        emitf("{\"fw\":{\"ok\":\"erase\",\"slot\":%d}}", (int)num);

    } else if (strcmp(op, "send") == 0) {
        int64_t slot = 0, node = 0, index = CB_JSONAPI_FW_INDEX_DEFAULT, sub = CB_JSONAPI_FW_SUB_DEFAULT;
        size_t size;
        uint32_t crc;
        char name[JSONAPI_FW_NAME_MAX + 1];

        if (!cj_obj_get(obj, "slot", &v) || !cj_as_i64(v, &slot) || !cj_obj_get(obj, "node", &v)
            || !cj_as_i64(v, &node) || node < 1 || node > 127) {
            emit_err("fw send: slot/node ungueltig");
            return;
        }
        if (cj_obj_get(obj, "index", &v)) {
            (void)cj_as_i64(v, &index);
        }
        if (cj_obj_get(obj, "sub", &v)) {
            (void)cj_as_i64(v, &sub);
        }
        if (fw->slot_info((int)slot, &size, &crc, name, sizeof(name)) != 0) {
            emit_err("fw send: Slot leer/ungueltig");
            return;
        }

        struct fw_send_ctx ctx = {.slot = (int)slot, .node = (int)node, .off = 0, .size = size, .last_emit = 0};
        uint32_t abort_code = 0;

        /* Blockiert poll() fuer die Dauer des Transfers; GTWA-Antworten
         * und PDO-Events puffern derweil (Ringpuffer/respHold). */
        if (cb_co_sdo_write_stream((uint8_t)node, (uint16_t)index, (uint8_t)sub, size, CB_JSONAPI_FW_SDO_TIMEOUT_MS,
                                   fw_send_read, &ctx, fw_send_progress, &ctx, &abort_code)
            != 0) {
            emitf("{\"err\":\"fw send: SDO-Abbruch 0x%08x\"}", (unsigned int)abort_code);
            return;
        }
        emitf("{\"fw\":{\"ok\":\"send\",\"slot\":%d,\"node\":%d}}", (int)slot, (int)node);

    } else {
        emit_err("fw: unbekanntes op");
    }
}

/* ------------------------------------------------------------------ */
/* Request-Dispatch                                                    */

static void
handle_request(const char* line, size_t len) {
    cj_t root, v;

    if (!cj_parse(line, len, &root) || !cj_is_obj(root)) {
        emit_err("Request ist kein JSON-Objekt");
        return;
    }

    if (cj_obj_get(root, "gtwa", &v)) {
        char cmd[CB_JSONAPI_GTWA_CMD_MAX];

        if (cj_is_str(v)) {
            size_t n = cj_as_str(v, cmd, sizeof(cmd));

            if (n != (size_t)-1 && n > 0) {
                gtwa_send_line(cmd, n);
            }
        } else if (cj_is_arr(v)) {
            size_t it = 0;
            cj_t e;

            while (cj_arr_next(v, &it, &e)) {
                size_t n = cj_as_str(e, cmd, sizeof(cmd));

                if (n != (size_t)-1 && n > 0) {
                    gtwa_send_line(cmd, n);
                }
            }
        } else {
            emit_err("gtwa: String oder Array von Strings erwartet");
        }
        return;
    }

    if (cj_obj_get(root, "mon", &v)) {
        if (cj_is_obj(v)) {
            mon_handle(v);
        } else {
            emit_err("mon: Objekt erwartet");
        }
        return;
    }

    if (cj_obj_get(root, "fw", &v)) {
        if (cj_is_obj(v)) {
            fw_handle(v);
        } else {
            emit_err("fw: Objekt erwartet");
        }
        return;
    }

    if (cj_obj_get(root, "ping", &v)) {
        emitf("{\"pong\":%.*s}", (int)v.len, v.s);
        return;
    }

    if (cj_obj_get(root, "info", &v)) {
        emitf("{\"info\":{\"ver\":1,\"gtwa\":true,\"monmax\":%d,\"fw\":%s,\"fwslots\":%d,\"fwslotcap\":%u}}",
              CB_JSONAPI_MON_MAX, (fw != NULL) ? "true" : "false", (fw != NULL) ? fw->slot_count() : 0,
              (fw != NULL) ? (unsigned int)fw->slot_capacity() : 0u);
        return;
    }

    emit_err("unbekannter Request (gtwa/mon/fw/ping/info)");
}

/* ------------------------------------------------------------------ */
/* API                                                                 */

int
jsonapi_init(const jsonapi_sink_t* sink, const jsonapi_fw_ops_t* fw_ops) {
    if (sink == NULL || sink->write == NULL) {
        return -EINVAL;
    }
    out_sink = *sink;
    fw = fw_ops;

    cb_co_gtwa_set_read(gtwa_sink, NULL);
    cb_co_set_raw_rx_hook(mon_rx_hook);
    return 0;
}

void
jsonapi_input(const void* buf, size_t len) {
    if (ring_buf_put(&in_rb, buf, (uint32_t)len) < len) {
        req_overflow = true; /* poll meldet den Fehler beim Zeilenende */
    }
}

void
jsonapi_poll(void) {
    uint8_t c;

    while (ring_buf_get(&in_rb, &c, 1) == 1) {
        if (c == '\n' || c == '\r') {
            if (req_overflow) {
                emit_err("Requestzeile zu lang/Ringpuffer voll");
            } else if (req_len > 0) {
                req_line[req_len] = '\0';
                handle_request(req_line, req_len);
            }
            req_len = 0;
            req_overflow = false;
            continue;
        }
        if (req_len < sizeof(req_line) - 1) {
            req_line[req_len++] = (char)c;
        } else {
            req_overflow = true;
        }
    }

    gtwa_pump();
    mon_pump();
}
