/**
 * canboss_berry.c
 *
 * Berry-VM mit od_*-Bindings (siehe canboss_berry.h) - gemeinsame
 * Schicht fuer alle Apps (Touch-Panel: Shell-Kommando "berry",
 * Monitor: interaktive REPL, siehe berry_repl.c).
 *
 * Remote-Zugriffe laufen ueber die blockierenden SDO-Transfers aus
 * lib/canopen/co_node.c, lokale OD-Zugriffe ueber die OD-Schnittstelle
 * von CANopenNode unter CO_LOCK_OD - dort liegen auch die per RPDO
 * empfangenen PDO-Werte (z.B. 0x2110 Analogeingaenge, 0x2111 Temperatur).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "berry.h"

#include "co_node.h" /* bringt CANopen.h (OD_*-Typen) mit */
#include "osal.h"

#include "canboss_berry.h" /* nach co_node.h (OD_t) */

/* groesster Datenpunkt (VISIBLE_STRING/OCTET) fuer SDO-Puffer */
#define CB_BERRY_DATA_MAX 64

static bvm* cb_vm;
static OD_t* cb_local_od;                 /* eigenes OD (canboss_berry_init) */
static uint8_t cb_node_ids[64];           /* Registry fuer od_nodes() */
static uint16_t cb_node_id_count;

/* ------------------------------------------------------------------ */
/* Hilfen                                                              */

static int64_t
cb_le_to_int(const uint8_t* data, size_t len) {
    uint64_t u = 0;
    for (size_t i = 0; i < len && i < 8; i++) {
        u |= (uint64_t)data[i] << (8u * i);
    }
    return (int64_t)u;
}

static void
cb_int_to_le(uint8_t* data, size_t len, int64_t v) {
    for (size_t i = 0; i < len; i++) {
        data[i] = (uint8_t)((uint64_t)v >> (8u * i));
    }
}

static int
cb_arg_error(bvm* vm, const char* msg) {
    be_raise(vm, "type_error", msg);
    be_return_nil(vm);
}

static int
cb_sdo_error(bvm* vm, uint32_t abort_code) {
    be_raise(vm, "sdo_error", cb_co_abort_str(abort_code));
    be_return_nil(vm);
}

/* ------------------------------------------------------------------ */
/* Remote-Zugriff per SDO                                              */

static int
cb_remote_read(bvm* vm, uint8_t* buf, size_t buf_size, size_t* got) {
    if (be_top(vm) < 3 || !be_isint(vm, 1) || !be_isint(vm, 2) || !be_isint(vm, 3)) {
        return cb_arg_error(vm, "od_read(node, index, sub)");
    }
    uint32_t abort = 0;
    if (cb_co_sdo_read((uint8_t)be_toint(vm, 1), (uint16_t)be_toint(vm, 2), (uint8_t)be_toint(vm, 3), buf, buf_size,
                       got, &abort)
        != 0) {
        return cb_sdo_error(vm, abort);
    }
    return -1; /* kein Fehler: Aufrufer pusht das Ergebnis */
}

static int
m_od_read(bvm* vm) {
    uint8_t buf[64];
    size_t got = 0;
    int err = cb_remote_read(vm, buf, sizeof(buf), &got);
    if (err >= 0) {
        return err;
    }
    if (got <= 8) {
        be_pushint(vm, (bint)cb_le_to_int(buf, got));
    } else {
        be_pushnstring(vm, (const char*)buf, got);
    }
    be_return(vm);
}

static int
m_od_reads(bvm* vm) {
    uint8_t buf[64];
    size_t got = 0;
    int err = cb_remote_read(vm, buf, sizeof(buf), &got);
    if (err >= 0) {
        return err;
    }
    /* NUL-terminierte VISIBLE_STRINGs sauber kappen */
    size_t n = 0;
    while (n < got && buf[n] != '\0') {
        n++;
    }
    be_pushnstring(vm, (const char*)buf, n);
    be_return(vm);
}

static int
m_od_readf(bvm* vm) {
    uint8_t buf[8];
    size_t got = 0;
    int err = cb_remote_read(vm, buf, sizeof(buf), &got);
    if (err >= 0) {
        return err;
    }
    if (got != 4) {
        return cb_arg_error(vm, "od_readf: Datenpunkt ist kein REAL32");
    }
    float f;
    memcpy(&f, buf, 4);
    be_pushreal(vm, (breal)f);
    be_return(vm);
}

static int
m_od_write(bvm* vm) {
    if (be_top(vm) < 4 || !be_isint(vm, 1) || !be_isint(vm, 2) || !be_isint(vm, 3)) {
        return cb_arg_error(vm, "od_write(node, index, sub, wert[, size])");
    }
    uint8_t node = (uint8_t)be_toint(vm, 1);
    uint16_t index = (uint16_t)be_toint(vm, 2);
    uint8_t sub = (uint8_t)be_toint(vm, 3);

    uint8_t buf[CB_BERRY_DATA_MAX];
    size_t len;

    if (be_isstring(vm, 4)) {
        const char* s = be_tostring(vm, 4);
        len = strlen(s);
        if (len > sizeof(buf)) {
            len = sizeof(buf);
        }
        memcpy(buf, s, len);
    } else if (be_isreal(vm, 4)) {
        float f = (float)be_toreal(vm, 4);
        memcpy(buf, &f, 4);
        len = 4;
    } else if (be_isint(vm, 4)) {
        if (be_top(vm) < 5 || !be_isint(vm, 5)) {
            return cb_arg_error(vm, "od_write: fuer Integer size angeben (1/2/4/8)");
        }
        len = (size_t)be_toint(vm, 5);
        if (len != 1 && len != 2 && len != 4 && len != 8) {
            return cb_arg_error(vm, "od_write: size muss 1/2/4/8 sein");
        }
        cb_int_to_le(buf, len, (int64_t)be_toint(vm, 4));
    } else {
        return cb_arg_error(vm, "od_write: wert muss int/real/string sein");
    }

    uint32_t abort = 0;
    if (cb_co_sdo_write(node, index, sub, buf, len, &abort) != 0) {
        return cb_sdo_error(vm, abort);
    }
    be_pushbool(vm, true);
    be_return(vm);
}

/* ------------------------------------------------------------------ */
/* Lokales OD (inkl. RPDO-Werte)                                       */

/* Liefert Laenge des Subeintrags oder 0 bei Fehler */
static size_t
cb_local_len(uint16_t index, uint8_t sub, OD_entry_t** entry_out) {
    if (cb_local_od == NULL) {
        return 0;
    }
    OD_entry_t* entry = OD_find(cb_local_od, index);
    if (entry == NULL) {
        return 0;
    }
    OD_IO_t io;
    if (OD_getSub(entry, sub, &io, true) != ODR_OK) {
        return 0;
    }
    *entry_out = entry;
    return (size_t)io.stream.dataLength;
}

static int
m_od_local_common(bvm* vm, bool as_float) {
    if (be_top(vm) < 2 || !be_isint(vm, 1) || !be_isint(vm, 2)) {
        return cb_arg_error(vm, "od_local(index, sub)");
    }
    uint16_t index = (uint16_t)be_toint(vm, 1);
    uint8_t sub = (uint8_t)be_toint(vm, 2);

    OD_entry_t* entry = NULL;
    size_t len = cb_local_len(index, sub, &entry);
    if (len == 0 || len > 64) {
        return cb_arg_error(vm, "od_local: Eintrag nicht vorhanden");
    }

    uint8_t buf[64];
    CO_t* co = (CO_t*)cb_co_handle();
    if (co != NULL) {
        CO_LOCK_OD(co->CANmodule);
    }
    ODR_t r = OD_get_value(entry, sub, buf, (OD_size_t)len, true);
    if (co != NULL) {
        CO_UNLOCK_OD(co->CANmodule);
    }
    if (r != ODR_OK) {
        return cb_arg_error(vm, "od_local: Lesen fehlgeschlagen");
    }

    if (as_float) {
        if (len != 4) {
            return cb_arg_error(vm, "od_localf: Eintrag ist kein REAL32");
        }
        float f;
        memcpy(&f, buf, 4);
        be_pushreal(vm, (breal)f);
    } else if (len <= 8) {
        be_pushint(vm, (bint)cb_le_to_int(buf, len));
    } else {
        size_t n = 0;
        while (n < len && buf[n] != '\0') {
            n++;
        }
        be_pushnstring(vm, (const char*)buf, n);
    }
    be_return(vm);
}

static int
m_od_local(bvm* vm) {
    return m_od_local_common(vm, false);
}

static int
m_od_localf(bvm* vm) {
    return m_od_local_common(vm, true);
}

static int
m_od_local_write(bvm* vm) {
    if (be_top(vm) < 3 || !be_isint(vm, 1) || !be_isint(vm, 2)) {
        return cb_arg_error(vm, "od_local_write(index, sub, wert[, size])");
    }
    uint16_t index = (uint16_t)be_toint(vm, 1);
    uint8_t sub = (uint8_t)be_toint(vm, 2);

    OD_entry_t* entry = NULL;
    size_t od_len = cb_local_len(index, sub, &entry);
    if (od_len == 0 || od_len > 64) {
        return cb_arg_error(vm, "od_local_write: Eintrag nicht vorhanden");
    }

    uint8_t buf[64];
    size_t len = od_len;
    memset(buf, 0, sizeof(buf));

    if (be_isstring(vm, 3)) {
        const char* s = be_tostring(vm, 3);
        size_t n = strlen(s);
        if (n > od_len) {
            n = od_len;
        }
        memcpy(buf, s, n);
    } else if (be_isreal(vm, 3)) {
        if (od_len != 4) {
            return cb_arg_error(vm, "od_local_write: Eintrag ist kein REAL32");
        }
        float f = (float)be_toreal(vm, 3);
        memcpy(buf, &f, 4);
    } else if (be_isint(vm, 3)) {
        if (od_len > 8) {
            return cb_arg_error(vm, "od_local_write: Eintrag ist kein Integer");
        }
        cb_int_to_le(buf, od_len, (int64_t)be_toint(vm, 3));
    } else {
        return cb_arg_error(vm, "od_local_write: wert muss int/real/string sein");
    }

    CO_t* co = (CO_t*)cb_co_handle();
    if (co != NULL) {
        CO_LOCK_OD(co->CANmodule);
    }
    ODR_t r = OD_set_value(entry, sub, buf, (OD_size_t)len, true);
    if (co != NULL) {
        CO_UNLOCK_OD(co->CANmodule);
    }
    if (r != ODR_OK) {
        return cb_arg_error(vm, "od_local_write: Schreiben fehlgeschlagen");
    }
    be_pushbool(vm, true);
    be_return(vm);
}

static int
m_od_nodes(bvm* vm) {
    be_newobject(vm, "list");
    for (uint16_t i = 0; i < cb_node_id_count; i++) {
        be_pushint(vm, cb_node_ids[i]);
        be_data_push(vm, -2);
        be_pop(vm, 1);
    }
    be_pop(vm, 1);
    be_return(vm);
}

/* ------------------------------------------------------------------ */
/* help(): Vorschlaege fuer die REPL/Shell                             */

static void
cb_help_write(const char* s) {
    be_writebuffer(s, strlen(s));
}

static int
m_help(bvm* vm) {
    cb_help_write("Berry " BERRY_VERSION " - https://berry-lang.github.io\n"
                  "\n"
                  "Sprache (Auswahl):\n"
                  "  1 + 2 * 3                        var x = 42   print(\"x =\", x)\n"
                  "  for i : 1..4 print(i) end        while x > 0  x -= 1  end\n"
                  "  def quad(a) return a * a end     quad(7)\n"
                  "  l = [1, 2, 3]   l.push(4)        m = {\"a\": 1}   m[\"b\"] = 2\n"
                  "  s = \"CAN\" + \"boss\"               string.format(\"0x%04X\", 4096)\n"
                  "  math.sin(math.pi / 2)            import json   json.dump(m)\n"
                  "\n"
                  "CANopen (od_*, Demo-Netzwerk: Knoten 16/32/48):\n"
                  "  od_nodes()                        Knoten aus eds/network.json\n"
                  "  od_read(16, 0x2103, 0)            SDO lesen -> int\n"
                  "  od_reads(16, 0x1008, 0)           SDO lesen -> string (Geraetename)\n"
                  "  od_readf(48, 0x2300, 1)           SDO lesen -> real (REAL32)\n"
                  "  od_write(16, 0x2101, 0, 300, 2)   SDO schreiben (int, size 1/2/4/8)\n"
                  "  od_write(16, 0x2102, 0, \"Neu\")    SDO schreiben (string)\n"
                  "  od_local(0x1017, 0)               eigenes OD (inkl. RPDO-Werte)\n"
                  "  od_local_write(0x1017, 0, 500, 2) eigenes OD schreiben\n"
                  "\n"
                  "exit oder Strg-D beendet die REPL.\n");
    (void)vm;
    be_return_nil(vm);
}

/* ------------------------------------------------------------------ */
/* VM + Ausfuehrung                                                    */

int
canboss_berry_exec(const char* code) {
    if (cb_vm == NULL || code == NULL) {
        return -1;
    }

    cb_mutex_lock(CB_MUTEX_BERRY);

    /* Erst als Ausdruck versuchen ("return (...)"), damit
     * `berry 1+2` direkt das Ergebnis liefert; sonst als Anweisung. */
    int ret;
    size_t n = strlen(code) + 16;
    char* expr = malloc(n);
    if (expr != NULL) {
        snprintf(expr, n, "return (%s)", code);
        ret = be_loadstring(cb_vm, expr);
        free(expr);
        if (ret != 0) {
            be_pop(cb_vm, 1); /* Compilerfehler verwerfen, als Anweisung versuchen */
            ret = be_loadstring(cb_vm, code);
        }
    } else {
        ret = be_loadstring(cb_vm, code);
    }

    if (ret == 0) {
        ret = be_pcall(cb_vm, 0);
    }

    if (ret != 0) {
        /* Nur Typ und Meldung, kein stack traceback (NDJSON-repl). */
        if (be_top(cb_vm) >= 2) {
            const char* type = be_tostring(cb_vm, -2);
            const char* arg = be_tostring(cb_vm, -1);

            if (type != NULL && type[0] != '\0') {
                be_writebuffer(type, strlen(type));
                if (arg != NULL && arg[0] != '\0') {
                    be_writebuffer(": ", 2);
                    be_writebuffer(arg, strlen(arg));
                }
                be_writebuffer("\n", 1);
            }
            be_pop(cb_vm, 2);
        }
    } else {
        if (!be_isnil(cb_vm, -1)) {
            const char* s = be_tostring(cb_vm, -1);
            be_writebuffer(s, strlen(s));
            be_writebuffer("\n", 1);
        }
        be_pop(cb_vm, 1);
    }

    cb_mutex_unlock(CB_MUTEX_BERRY);
    return ret == 0 ? 0 : -1;
}

void
canboss_berry_register(void* vm_arg, OD_t* local_od) {
    bvm* vm = (bvm*)vm_arg;

    cb_local_od = local_od;

    be_regfunc(vm, "od_read", m_od_read);
    be_regfunc(vm, "od_reads", m_od_reads);
    be_regfunc(vm, "od_readf", m_od_readf);
    be_regfunc(vm, "od_write", m_od_write);
    be_regfunc(vm, "od_local", m_od_local);
    be_regfunc(vm, "od_localf", m_od_localf);
    be_regfunc(vm, "od_local_write", m_od_local_write);
    be_regfunc(vm, "od_nodes", m_od_nodes);
}

void
canboss_berry_init(OD_t* local_od) {
    if (cb_vm != NULL) {
        cb_local_od = local_od;
        return;
    }
    cb_vm = be_vm_new();

    canboss_berry_register(cb_vm, local_od);
    be_regfunc(cb_vm, "help", m_help);
}

void
canboss_berry_set_nodes(const uint8_t* ids, uint16_t count) {
    if (count > (uint16_t)(sizeof(cb_node_ids) / sizeof(cb_node_ids[0]))) {
        count = (uint16_t)(sizeof(cb_node_ids) / sizeof(cb_node_ids[0]));
    }
    if (ids != NULL) {
        memcpy(cb_node_ids, ids, count);
        cb_node_id_count = count;
    }
}

void*
canboss_berry_vm(void) {
    return cb_vm;
}
