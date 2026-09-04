/**
 * ndjson.c - NDJSON-Interface fuer Webapps, siehe ndjson.h
 *
 * Zephyr-only (Ringpuffer, Spinlock, k_sleep); genutzt von
 * apps/gateway (USB-CDC/Konsole) und perspektivisch canBLEberry (BLE).
 *
 * Datenfluesse:
 *   Transport-RX (beliebiger Kontext) -> in_rb  -> poll: Zeile -> Dispatch
 *   GTWA-Antworten (Mainline-Thread)  -> gtwa_rb-> poll: {"gtwa":{"seq":…}}
 *   Raw-RX-Hook (CAN-RX-Thread)       -> pdo_rb -> poll: {"pdo":{...}}
 *   fw send: blockiert in poll, Fortschritt als {"fw":{"prog":...}}
 */

#include "ndjson.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h> /* strtoul (fw end: CRC aus dem Hex-String) */
#include <string.h>

#include "co_node.h"
#include "osal.h"

#include "ndjson_parse.h"
#include "slcan.h"

#if ((CO_CONFIG_GTW)&CO_CONFIG_GTW_ASCII) == 0
#error "lib/host_io braucht CO_CONFIG_GTW_ASCII (per CO_DRIVER_CUSTOM aktivieren, siehe apps/gateway)"
#endif

#define CB_NDJSON_LINE_MAX           3584 /* laengste Requestzeile (fw data, ~3 KiB Base64) */
#define CB_NDJSON_GTWA_CMD_MAX       200  /* eine CiA309-3-Kommandozeile */
#define CB_NDJSON_GTWA_BATCH_CAP     16   /* fest: Groesse der Sammelpuffer */
#define CB_NDJSON_GTWA_BATCH_MAX     1    /* Default batmax (Laufzeit, {"gtwa":{"batmax":n}}) */
#define CB_NDJSON_GTWA_BATCH_VAL     96   /* Nutzlast einer gesammelten GTWA-Antwort */
#define CB_NDJSON_GTWA_BATCH_WAIT_MS 2000 /* Wartezeit je Batch-Kommando */
#define CB_NDJSON_GTWA_DRAIN_QUIET_MS 30  /* help/log: Ende nach Ruhepause */
#define CB_NDJSON_GTWA_LED_WAIT_MS    150 /* erste CR-Zeile von "led" */
#define CB_NDJSON_IN_RB_SIZE         4608
#define CB_NDJSON_GTWA_RB_SIZE       1024
#define CB_NDJSON_PDO_RB_SIZE        (32 * sizeof(struct pdo_rec))
#define CB_NDJSON_MON_MAX            16
#define CB_NDJSON_MON_RATE_MS        100 /* Default-Mindestabstand je COB */

#ifndef CB_NDJSON_FW_SDO_TIMEOUT_MS
#define CB_NDJSON_FW_SDO_TIMEOUT_MS 2000
#endif
#ifndef CB_NDJSON_FW_INDEX_DEFAULT
#define CB_NDJSON_FW_INDEX_DEFAULT 0x1F50 /* CiA 302 Program data */
#endif
#ifndef CB_NDJSON_FW_SUB_DEFAULT
#define CB_NDJSON_FW_SUB_DEFAULT 1
#endif

struct pdo_rec {
    uint16_t cob;
    uint8_t dlc;
    uint8_t data[8];
    uint32_t t_ms;
};

static ndjson_sink_t out_sink;
static const ndjson_fw_ops_t* fw;

RING_BUF_DECLARE(in_rb, CB_NDJSON_IN_RB_SIZE);
RING_BUF_DECLARE(gtwa_rb, CB_NDJSON_GTWA_RB_SIZE);
RING_BUF_DECLARE(pdo_rb, CB_NDJSON_PDO_RB_SIZE);

static char req_line[CB_NDJSON_LINE_MAX];
static size_t req_len;
static bool req_overflow;

static char gtwa_line[256];
static size_t gtwa_len;

/* Offener Array-Batch: Antworten zu {"gtwa":[cmd, …]} in einem Objekt */
static uint32_t gtwa_batch_seq[CB_NDJSON_GTWA_BATCH_CAP];
static char gtwa_batch_val[CB_NDJSON_GTWA_BATCH_CAP][CB_NDJSON_GTWA_BATCH_VAL];
static uint8_t gtwa_batch_have[CB_NDJSON_GTWA_BATCH_CAP];
static unsigned gtwa_batch_n;
static unsigned gtwa_batch_got;
static unsigned gtwa_batmax = CB_NDJSON_GTWA_BATCH_MAX;
static unsigned gtwa_seq; /* naechste zu vergebende Sequenz, Start 0 (0..9999) */
static uint32_t gtwa_unseq_wait = UINT32_MAX; /* aus; sonst seq von help/log */
static bool gtwa_unseq_got;                   /* [seq]-Antwort waehrend gtwa_drain */
static unsigned gtwa_unseq_lines;             /* Freitextzeilen waehrend Drain */

/* PDO-Monitor-Tabelle (Schreiber: poll; Leser: CAN-RX-Thread) */
static struct k_spinlock mon_lock;
static struct {
    uint16_t cob;
    uint32_t last_ms;
} mon_tab[CB_NDJSON_MON_MAX];
static int mon_count;
static uint32_t mon_rate_ms = CB_NDJSON_MON_RATE_MS;
static uint32_t mon_dropped;

/* fw-Upload-Zustand (nur poll-Thread) */
static bool fw_uploading;
static size_t fw_expect, fw_got;
static char fw_name[NDJSON_FW_NAME_MAX + 1];

/* Berry-REPL (optional, ndjson_set_repl) */
static ndjson_repl_exec_t repl_exec;
static void* repl_user;
static char repl_line[256];
static size_t repl_len;

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

/* "[123] rest" -> seq und Rest hinter dem Token (Spaces nach ] skip). */
static bool
gtwa_split_seq(const char* s, size_t len, uint32_t* seq, const char** rest, size_t* rest_len) {
    size_t i;
    uint32_t v = 0;
    bool any = false;

    if (len < 3 || s[0] != '[') {
        return false;
    }
    i = 1;
    while (i < len && s[i] >= '0' && s[i] <= '9') {
        uint32_t d = (uint32_t)(s[i] - '0');

        if (v > (0xFFFFFFFFu - d) / 10u) {
            return false;
        }
        v = v * 10u + d;
        any = true;
        i++;
    }
    if (!any || i >= len || s[i] != ']') {
        return false;
    }
    i++;
    while (i < len && (s[i] == ' ' || s[i] == '\t')) {
        i++;
    }
    *seq = v;
    *rest = s + i;
    *rest_len = len - i;
    return true;
}

/* Ganze Antwort ein JSON-Number-Token (optional +/-, Ganz- oder Dezimalzahl)?
 * Hex, Exponent und fuehrende Nullen (ausser "0") gelten als String.
 * out erhaelt das emit-fertige Token (ohne '+'). */
static bool
gtwa_is_number(const char* s, size_t len, char* out, size_t out_size) {
    size_t i = 0;
    size_t o = 0;

    if (len == 0 || out_size < 2) {
        return false;
    }
    if (s[0] == '+' || s[0] == '-') {
        if (s[0] == '-') {
            out[o++] = '-';
        }
        i = 1;
        if (i >= len) {
            return false;
        }
    }
    if (s[i] == '0') {
        out[o++] = '0';
        i++;
        if (i < len && s[i] >= '0' && s[i] <= '9') {
            return false;
        }
    } else if (s[i] >= '1' && s[i] <= '9') {
        while (i < len && s[i] >= '0' && s[i] <= '9') {
            if (o + 1 >= out_size) {
                return false;
            }
            out[o++] = s[i++];
        }
    } else {
        return false;
    }
    if (i < len && s[i] == '.') {
        if (i + 1 >= len || s[i + 1] < '0' || s[i + 1] > '9') {
            return false;
        }
        if (o + 1 >= out_size) {
            return false;
        }
        out[o++] = s[i++];
        while (i < len && s[i] >= '0' && s[i] <= '9') {
            if (o + 1 >= out_size) {
                return false;
            }
            out[o++] = s[i++];
        }
    }
    if (i != len || o == 0 || (o == 1 && out[0] == '-')) {
        return false;
    }
    out[o] = '\0';
    return true;
}

/* "seq":<zahl|string> nach buf. Rueckgabe wie snprintf. */
static int
gtwa_fmt_kv(char* buf, size_t buf_size, uint32_t seq, const char* val, size_t val_len) {
    char num[40];

    if (gtwa_is_number(val, val_len, num, sizeof(num))) {
        return snprintf(buf, buf_size, "\"%u\":%s", (unsigned)seq, num);
    }
    {
        char q[200];

        cj_quote(val, val_len, q, sizeof(q));
        return snprintf(buf, buf_size, "\"%u\":%s", (unsigned)seq, q);
    }
}

/* Eine Kommandozeile (ohne '\n') ins Gateway schieben.
 * CANopenNode (CiA 309-3) verlangt zwingend "["<sequence>"]" als erstes
 * Token; ohne das kommt ERROR:101. {"gtwa":"help"} und die Webapp-Hilfe
 * liefern nur das Kommando — Sequenz hier ergaenzen.
 * seq_out (optional) erhaelt die verwendete Sequenznummer.
 * Rueckgabe true, wenn die Zeile im Gateway-Fifo liegt. */
static bool
gtwa_send_line(const char* cmd, size_t len, uint32_t* seq_out) {
    char lined[CB_NDJSON_GTWA_CMD_MAX + 16];
    const char* p;
    size_t rest;
    int tries = 500; /* max ~500 ms auf Fifo-Platz warten */
    uint32_t used = 0;
    const char* dummy;
    size_t dummy_len;

    while (len > 0 && (cmd[len - 1] == '\r' || cmd[len - 1] == ' ' || cmd[len - 1] == '\t')) {
        len--;
    }
    while (len > 0 && (*cmd == ' ' || *cmd == '\t')) {
        cmd++;
        len--;
    }
    if (len == 0) {
        return false;
    }

    if (cmd[0] != '[') {
        used = gtwa_seq;
        gtwa_seq = (gtwa_seq + 1U) % 10000U;
        int w = snprintf(lined, sizeof(lined), "[%u] %.*s", (unsigned)used, (int)len, cmd);

        if (w <= 0 || (size_t)w >= sizeof(lined)) {
            emit_err("gtwa: Kommando zu lang");
            return false;
        }
        p = lined;
        rest = (size_t)w;
    } else {
        if (!gtwa_split_seq(cmd, len, &used, &dummy, &dummy_len)) {
            used = 0;
        }
        p = cmd;
        rest = len;
    }

    while (rest > 0 && tries-- > 0) {
        int n = cb_co_gtwa_write(p, rest);

        if (n < 0) {
            emit_err("gtwa: Gateway laeuft nicht (CANopen offline oder CNT_GTWA=0)");
            return false;
        }
        p += n;
        rest -= (size_t)n;
        if (rest > 0) {
            k_sleep(K_MSEC(1));
        }
    }
    if (rest > 0 || cb_co_gtwa_write("\n", 1) != 1) {
        emit_err("gtwa: Kommando-Fifo voll");
        return false;
    }
    if (seq_out != NULL) {
        *seq_out = used;
    }
    return true;
}

static void
gtwa_emit_one(uint32_t seq, const char* val, size_t val_len) {
    char kv[240];

    if (gtwa_fmt_kv(kv, sizeof(kv), seq, val, val_len) > 0) {
        emitf("{\"gtwa\":{%s}}", kv);
    }
}

/* Eine fertige GTWA-Antwortzeile: in den offenen Batch legen oder
 * sofort als {"gtwa":{"seq":…}} ausgeben. Zeilen ohne [seq] (help)
 * kommen als String-Value. */
static void
gtwa_handle_line(const char* line, size_t len) {
    uint32_t seq;
    const char* rest;
    size_t rest_len;

    if (gtwa_split_seq(line, len, &seq, &rest, &rest_len)) {
        if (gtwa_unseq_wait != UINT32_MAX && seq == gtwa_unseq_wait) {
            gtwa_unseq_got = true;
        }
        if (gtwa_batch_n > 0) {
            for (unsigned i = 0; i < gtwa_batch_n; i++) {
                if (!gtwa_batch_have[i] && gtwa_batch_seq[i] == seq) {
                    size_t n = rest_len;

                    if (n >= CB_NDJSON_GTWA_BATCH_VAL) {
                        n = CB_NDJSON_GTWA_BATCH_VAL - 1;
                    }
                    memcpy(gtwa_batch_val[i], rest, n);
                    gtwa_batch_val[i][n] = '\0';
                    gtwa_batch_have[i] = 1;
                    gtwa_batch_got++;
                    return;
                }
            }
        }
        gtwa_emit_one(seq, rest, rest_len);
        return;
    }

    {
        char q[420];

        cj_quote(line, len, q, sizeof(q));
        emitf("{\"gtwa\":%s}", q);
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

        gtwa_handle_line(gtwa_line, gtwa_len);
        gtwa_len = 0;
    }
}

static void
gtwa_batch_emit(void) {
    out_raw("{\"gtwa\":{", 9);
    for (unsigned i = 0; i < gtwa_batch_n; i++) {
        char kv[240];
        const char* val = gtwa_batch_have[i] ? gtwa_batch_val[i] : "ERROR:timeout";
        int m = gtwa_fmt_kv(kv, sizeof(kv), gtwa_batch_seq[i], val, strlen(val));

        if (m > 0) {
            if (i > 0) {
                out_raw(",", 1);
            }
            out_raw(kv, (size_t)m);
        }
    }
    out_raw("}}\n", 3);
    gtwa_batch_n = 0;
    gtwa_batch_got = 0;
}

/* Wartet auf die zum Array gehoerenden [seq]-Antworten und sendet
 * genau eine NDJSON-Zeile {"gtwa":{"seq":…, …}}. Blockiert poll
 * (wie "fw send"); help/led-Zeilen ohne [seq] gehen weiter sofort raus. */
static void
gtwa_batch_wait(void) {
    uint32_t deadline = k_uptime_get_32() + gtwa_batch_n * (uint32_t)CB_NDJSON_GTWA_BATCH_WAIT_MS;

    while (gtwa_batch_got < gtwa_batch_n) {
        gtwa_pump();
        if (gtwa_batch_got >= gtwa_batch_n) {
            break;
        }
        if ((int32_t)(k_uptime_get_32() - deadline) >= 0) {
            break;
        }
        k_sleep(K_MSEC(1));
    }
    gtwa_batch_emit();
}

/* ------------------------------------------------------------------ */
/* PDO-Monitor                                                         */

/* Raw-RX-Hook: laeuft im CAN-RX-Thread, muss kurz bleiben.
 * Verteilt an SLCAN (alle Frames inkl. RTR) und den PDO-Monitor
 * (nur Datenframes). */
static void
mon_rx_hook(uint16_t ident, const uint8_t* data, uint8_t dlc, bool rtr) {
    uint32_t now_ms = (uint32_t)(cb_now_us() / 1000u);
    bool take = false;

    slcan_rx(ident, data, dlc, rtr);
    if (rtr) {
        return;
    }

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
                if (!known && mon_count < CB_NDJSON_MON_MAX) {
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
            char name[NDJSON_FW_NAME_MAX + 1];
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
        int64_t slot = 0, node = 0, index = CB_NDJSON_FW_INDEX_DEFAULT, sub = CB_NDJSON_FW_SUB_DEFAULT;
        size_t size;
        uint32_t crc;
        char name[NDJSON_FW_NAME_MAX + 1];

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
        if (cb_co_sdo_write_stream((uint8_t)node, (uint16_t)index, (uint8_t)sub, size, CB_NDJSON_FW_SDO_TIMEOUT_MS,
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
/* Berry-REPL                                                          */

static unsigned repl_emitted;

static void
repl_emit(const char* s, size_t len) {
    char num[40];

    if (gtwa_is_number(s, len, num, sizeof(num))) {
        emitf("{\"repl\":%s}", num);
    } else {
        char q[420];

        cj_quote(s, len, q, sizeof(q));
        emitf("{\"repl\":%s}", q);
    }
    repl_emitted++;
}

static void
repl_emit_msg(const char* msg) {
    repl_emit(msg, strlen(msg));
}

static void
repl_flush_line(void) {
    if (repl_len == 0) {
        return;
    }
    repl_emit(repl_line, repl_len);
    repl_len = 0;
}

void
ndjson_repl_out(const char* buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = buf[i];

        if (c == '\r') {
            continue;
        }
        if (c == '\n' || repl_len >= sizeof(repl_line) - 1) {
            if (c != '\n') {
                repl_line[repl_len++] = c;
            }
            repl_flush_line();
            continue;
        }
        repl_line[repl_len++] = c;
    }
}

void
ndjson_set_repl(ndjson_repl_exec_t exec, void* user) {
    repl_exec = exec;
    repl_user = user;
}

static void
repl_run(const char* code) {
    int ret;

    repl_emitted = 0;
    ret = repl_exec(repl_user, code);
    repl_flush_line(); /* Restzeile ohne '\n' noch ausgeben */
    if (ret != 0 && repl_emitted == 0) {
        repl_emit_msg("Ausfuehrung fehlgeschlagen");
    }
}

static void
repl_handle(cj_t v) {
    static char code[512];

    if (repl_exec == NULL) {
        repl_emit_msg("nicht verfuegbar (Firmware ohne Berry)");
        return;
    }

    if (cj_is_str(v)) {
        if (cj_as_str(v, code, sizeof(code)) == (size_t)-1) {
            repl_emit_msg("Kommando zu lang");
        } else {
            repl_run(code);
        }
    } else if (cj_is_arr(v)) {
        size_t it = 0;
        cj_t e;

        while (cj_arr_next(v, &it, &e)) {
            if (cj_as_str(e, code, sizeof(code)) == (size_t)-1) {
                repl_emit_msg("Kommando zu lang");
            } else {
                repl_run(code);
            }
        }
    } else {
        repl_emit_msg("String oder Array von Strings erwartet");
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
        char cmd[CB_NDJSON_GTWA_CMD_MAX];

        if (cj_is_str(v)) {
            size_t n = cj_as_str(v, cmd, sizeof(cmd));

            if (n != (size_t)-1 && n > 0) {
                (void)gtwa_send_line(cmd, n, NULL);
            }
        } else if (cj_is_arr(v)) {
            size_t it = 0;
            cj_t e;

            gtwa_batch_n = 0;
            gtwa_batch_got = 0;

            while (cj_arr_next(v, &it, &e)) {
                size_t n = cj_as_str(e, cmd, sizeof(cmd));
                uint32_t seq = 0;

                if (n == (size_t)-1 || n == 0) {
                    continue;
                }
                if (!gtwa_send_line(cmd, n, &seq)) {
                    if (gtwa_batch_n > 0) {
                        gtwa_batch_wait();
                    }
                    continue;
                }
                gtwa_batch_seq[gtwa_batch_n] = seq;
                gtwa_batch_have[gtwa_batch_n] = 0;
                gtwa_batch_val[gtwa_batch_n][0] = '\0';
                gtwa_batch_n++;
                if (gtwa_batch_n >= gtwa_batmax) {
                    gtwa_batch_wait();
                }
            }
            if (gtwa_batch_n > 0) {
                gtwa_batch_wait();
            }
        } else if (cj_is_obj(v)) {
            cj_t b;
            int64_t n;
            bool have_batmax = false;
            bool have_seq = false;

            if (cj_obj_get(v, "batmax", &b)) {
                if (!cj_as_i64(b, &n) || n < 1 || n > (int64_t)CB_NDJSON_GTWA_BATCH_CAP) {
                    emitf("{\"err\":\"gtwa: batmax 1..%u erwartet\"}", (unsigned)CB_NDJSON_GTWA_BATCH_CAP);
                    return;
                }
                gtwa_batmax = (unsigned)n;
                have_batmax = true;
            }
            if (cj_obj_get(v, "seq", &b)) {
                if (!cj_as_i64(b, &n) || n < 0 || n > 9999) {
                    emit_err("gtwa: seq 0..9999 erwartet");
                    return;
                }
                gtwa_seq = (unsigned)n;
                have_seq = true;
            }
            if (!have_batmax && !have_seq) {
                emit_err("gtwa: batmax oder seq erwartet");
                return;
            }
            if (have_batmax && have_seq) {
                emitf("{\"gtwa\":{\"batmax\":%u,\"seq\":%u}}", gtwa_batmax, gtwa_seq);
            } else if (have_batmax) {
                emitf("{\"gtwa\":{\"batmax\":%u}}", gtwa_batmax);
            } else {
                emitf("{\"gtwa\":{\"seq\":%u}}", gtwa_seq);
            }
        } else {
            emit_err("gtwa: String, Array oder {\"batmax\":n,\"seq\":n} erwartet");
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

    if (cj_obj_get(root, "repl", &v)) {
        repl_handle(v);
        return;
    }

    if (cj_obj_get(root, "ping", &v)) {
        emitf("{\"pong\":%.*s}", (int)v.len, v.s);
        return;
    }

    if (cj_obj_get(root, "nodstat", &v)) {
        int64_t id;
        cb_co_nodstat_ent_t tab[127];
        int n;
        bool first = true;

        if (!cj_as_i64(v, &id) || id < 0 || id > 127) {
            emit_err("nodstat: 0 (alle) oder Node-ID 1..127 erwartet");
            return;
        }

        n = cb_co_nodstat((uint8_t)id, tab, sizeof(tab) / sizeof(tab[0]));
        if (n == -ENODEV) {
            emit_err("nodstat: CANopen offline");
            return;
        }
        if (n < 0) {
            emit_err("nodstat: kein Heartbeat-Consumer konfiguriert");
            return;
        }
        if (n > (int)(sizeof(tab) / sizeof(tab[0]))) {
            n = (int)(sizeof(tab) / sizeof(tab[0]));
        }

        out_raw("{\"nodstat\":[", 12);
        for (int i = 0; i < n; i++) {
            const char* st;
            char item[28];
            int m;

            switch (tab[i].nmt_state) {
            case 0:
                st = "init";
                break;
            case 4:
                st = "stop";
                break;
            case 5:
                st = "op";
                break;
            case 127:
                st = "pre";
                break;
            default:
                st = "n/a";
                break;
            }
            m = snprintf(item, sizeof(item), "%s{\"%u\":\"%s\"}", first ? "" : ",", tab[i].node_id, st);

            if (m > 0) {
                out_raw(item, (size_t)m);
            }
            first = false;
        }
        out_raw("]}\n", 3);
        return;
    }

    if (cj_obj_get(root, "info", &v)) {
        emitf("{\"info\":{\"ver\":1,\"gtwa\":true,\"slcan\":true,\"nodstat\":true,\"repl\":%s,\"monmax\":%d,"
              "\"batmax\":%u,\"fw\":%s,\"fwslots\":%d,\"fwslotcap\":%u}}",
              (repl_exec != NULL) ? "true" : "false", CB_NDJSON_MON_MAX, gtwa_batmax,
              (fw != NULL) ? "true" : "false", (fw != NULL) ? fw->slot_count() : 0,
              (fw != NULL) ? (unsigned int)fw->slot_capacity() : 0u);
        return;
    }

    emit_err("unbekannter Request (gtwa/mon/fw/repl/ping/nodstat/info)");
}

/* ------------------------------------------------------------------ */
/* API                                                                 */

int
ndjson_init(const ndjson_sink_t* sink, const ndjson_fw_ops_t* fw_ops) {
    if (sink == NULL || sink->write == NULL) {
        return -EINVAL;
    }
    out_sink = *sink;
    fw = fw_ops;

    cb_co_gtwa_set_read(gtwa_sink, NULL);
    cb_co_set_raw_rx_hook(mon_rx_hook);
    slcan_init(out_raw);
    return 0;
}

void
ndjson_input(const void* buf, size_t len) {
    if (ring_buf_put(&in_rb, buf, (uint32_t)len) < len) {
        req_overflow = true; /* poll meldet den Fehler beim Zeilenende */
    }
}

void
ndjson_poll(void) {
    uint8_t c;

    while (ring_buf_get(&in_rb, &c, 1) == 1) {
        if (c == '\n' || c == '\r') {
            if (req_overflow) {
                emit_err("Requestzeile zu lang/Ringpuffer voll");
            } else if (req_len > 0) {
                size_t skip = 0;

                req_line[req_len] = '\0';
                while (skip < req_len && (req_line[skip] == ' ' || req_line[skip] == '\t')) {
                    skip++;
                }
                if (skip < req_len && req_line[skip] == '{') {
                    /* NDJSON-Request */
                    handle_request(&req_line[skip], req_len - skip);
                } else if (skip < req_len) {
                    /* keine '{': SLCAN-ASCII (Lawicel), siehe slcan.h */
                    slcan_line(&req_line[skip], req_len - skip);
                }
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
    slcan_pump();
}
