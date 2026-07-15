/**
 * canboss_berry.c
 *
 * Berry-VM mit od_*-Bindings (siehe canboss_berry.h) und
 * Zephyr-Shell-Kommando "berry".
 *
 * Remote-Zugriffe laufen ueber die blockierenden SDO-Transfers aus
 * lib/canopen/co_node.c (mit dem SDO-Worker der UI serialisiert), lokale
 * OD-Zugriffe ueber die OD-Schnittstelle von CANopenNode unter
 * CO_LOCK_OD - dort liegen auch die per RPDO empfangenen PDO-Werte
 * (z.B. 0x2110 Analogeingaenge, 0x2111 Temperatur).
 */

#include "canboss_berry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "berry.h"

#include "CANopen.h"
#include "canboss_master.h"

#include "canboss_types.h"
#include "co_node.h"
#include "osal.h"

static bvm* cb_vm;

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

    uint8_t buf[CANBOSS_DP_DATA_MAX];
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
    OD_entry_t* entry = OD_find(canboss_master, index);
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
    for (uint16_t i = 0; i < canboss_node_count; i++) {
        be_pushint(vm, canboss_nodes[i]->node_id);
        be_data_push(vm, -2);
        be_pop(vm, 1);
    }
    be_pop(vm, 1);
    be_return(vm);
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
        be_dumpexcept(cb_vm); /* Ausgabe ueber be_writebuffer -> Senke */
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
canboss_berry_init(void) {
    if (cb_vm != NULL) {
        return;
    }
    cb_vm = be_vm_new();

    be_regfunc(cb_vm, "od_read", m_od_read);
    be_regfunc(cb_vm, "od_reads", m_od_reads);
    be_regfunc(cb_vm, "od_readf", m_od_readf);
    be_regfunc(cb_vm, "od_write", m_od_write);
    be_regfunc(cb_vm, "od_local", m_od_local);
    be_regfunc(cb_vm, "od_localf", m_od_localf);
    be_regfunc(cb_vm, "od_local_write", m_od_local_write);
    be_regfunc(cb_vm, "od_nodes", m_od_nodes);
}

/* ------------------------------------------------------------------ */
/* Shell-Kommando: berry <skript>                                      */

static void
cb_shell_sink(void* user, const char* buf, size_t len) {
    const struct shell* sh = (const struct shell*)user;
    shell_fprintf(sh, SHELL_NORMAL, "%.*s", (int)len, buf);
}

static int
cmd_berry(const struct shell* sh, size_t argc, char** argv) {
    if (cb_vm == NULL) {
        shell_error(sh, "Berry-VM nicht initialisiert");
        return -1;
    }

    /* Argumente wieder zu einem Skripttext zusammensetzen */
    static char code[CONFIG_SHELL_CMD_BUFF_SIZE];
    code[0] = '\0';
    size_t pos = 0;
    for (size_t i = 1; i < argc; i++) {
        int w = snprintf(code + pos, sizeof(code) - pos, "%s%s", i > 1 ? " " : "", argv[i]);
        if (w < 0 || (size_t)w >= sizeof(code) - pos) {
            break;
        }
        pos += (size_t)w;
    }

    cb_berry_set_sink(cb_shell_sink, (void*)sh);
    int ret = canboss_berry_exec(code);
    cb_berry_set_sink(NULL, NULL);
    return ret;
}

SHELL_CMD_ARG_REGISTER(berry, NULL,
                       "Berry-Skript ausfuehren, z.B.: berry print(od_read(127,0x1017,0))",
                       cmd_berry, 2, 32);
