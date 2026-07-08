/**
 * canboss_sdo_host.c
 *
 * SDO-Mock fuer den Linux/SDL2-Host-Build: implementiert die
 * canboss_sdo-Schnittstelle ohne CANopenNode/FreeRTOS.
 *
 * Leseauftraege liefern simulierte Werte passend zum Datentyp des
 * Datenpunkts (Nachschlag in der generierten Registry): Integer laufen
 * langsam im Limitbereich, BOOLs togglen, REAL32 pendelt, Strings sind
 * fix. Geschriebene Werte werden gespeichert und danach zurueckgelesen —
 * so laesst sich das komplette Widget-Verhalten am PC durchspielen.
 */

#include "canboss_sdo.h"
#include "canboss_types.h"
#include "lvgl/lvgl.h"

#include <stdio.h>
#include <string.h>

#define HOST_STORE_MAX 256u

typedef struct {
    bool used;
    uint8_t node_id;
    uint16_t index;
    uint8_t sub;
    uint8_t data[CANBOSS_DP_DATA_MAX];
    size_t len;
} host_entry_t;

static host_entry_t host_store[HOST_STORE_MAX];

static host_entry_t*
host_store_find(uint8_t node_id, uint16_t index, uint8_t sub, bool create) {
    host_entry_t* free_slot = NULL;
    for (uint32_t i = 0; i < HOST_STORE_MAX; i++) {
        host_entry_t* e = &host_store[i];
        if (e->used && e->node_id == node_id && e->index == index && e->sub == sub) {
            return e;
        }
        if (!e->used && free_slot == NULL) {
            free_slot = e;
        }
    }
    if (create && free_slot != NULL) {
        memset(free_slot, 0, sizeof(*free_slot));
        free_slot->used = true;
        free_slot->node_id = node_id;
        free_slot->index = index;
        free_slot->sub = sub;
        return free_slot;
    }
    return NULL;
}

/* Datenpunkt-Beschreibung aus der generierten Registry suchen */
static const canboss_dp_t*
host_find_dp(uint8_t node_id, uint16_t index, uint8_t sub) {
    for (uint16_t n = 0; n < canboss_node_count; n++) {
        const canboss_node_desc_t* node = canboss_nodes[n];
        if (node->node_id != node_id) {
            continue;
        }
        for (uint16_t i = 0; i < node->dp_count; i++) {
            if (node->dps[i].index == index && node->dps[i].sub == sub) {
                return &node->dps[i];
            }
        }
    }
    return NULL;
}

static size_t
host_dtype_size(uint8_t dtype) {
    switch ((canboss_dtype_t)dtype) {
        case CB_DT_BOOL:
        case CB_DT_I8:
        case CB_DT_U8: return 1;
        case CB_DT_I16:
        case CB_DT_U16: return 2;
        case CB_DT_I32:
        case CB_DT_U32:
        case CB_DT_F32: return 4;
        case CB_DT_I64:
        case CB_DT_U64: return 8;
        default: return 0;
    }
}

/* Simulierten Wert fuer einen (nicht beschriebenen) Datenpunkt erzeugen */
static void
host_simulate(const canboss_dp_t* dp, uint16_t index, uint8_t sub, canboss_sdo_req_t* req) {
    uint32_t t = lv_tick_get() / 1000u; /* Sekundentakt */
    uint32_t seed = ((uint32_t)index << 8) ^ sub;

    if (dp == NULL) {
        /* Unbekannter Datenpunkt: 32-Bit-Zaehler */
        uint32_t v = seed + t;
        memcpy((void*)req->data, &v, 4);
        req->len = 4;
        return;
    }

    switch ((canboss_dtype_t)dp->dtype) {
        case CB_DT_BOOL: {
            req->data[0] = (uint8_t)(((t / 4u) + seed) & 1u);
            req->len = 1;
            break;
        }
        case CB_DT_STR: {
            char text[CANBOSS_DP_DATA_MAX];
            int n = snprintf(text, sizeof(text), "Host %04X.%02X", index, sub);
            memcpy((void*)req->data, text, (size_t)n);
            req->len = (size_t)n;
            break;
        }
        case CB_DT_OCTET: {
            for (uint32_t i = 0; i < 4; i++) {
                req->data[i] = (uint8_t)(seed + t + i);
            }
            req->len = 4;
            break;
        }
        case CB_DT_F32: {
            /* Dreieckskurve 0..100.0 */
            uint32_t phase = (t + seed) % 200u;
            float f = (phase < 100u ? (float)phase : (float)(200u - phase));
            memcpy((void*)req->data, &f, 4);
            req->len = 4;
            break;
        }
        default: {
            /* Integer: Dreieckskurve im Limitbereich (Default-Limits der
             * Generatortabelle koennen sehr gross sein -> dann 0..1000) */
            int64_t lo = dp->min;
            int64_t hi = dp->max;
            if (hi - lo > 100000 || hi <= lo) {
                lo = 0;
                hi = 1000;
            }
            uint64_t span = (uint64_t)(hi - lo);
            uint64_t phase = (t * (1u + span / 60u) + seed) % (2u * span);
            int64_t v = (phase < span) ? lo + (int64_t)phase : hi - (int64_t)(phase - span);
            size_t len = host_dtype_size(dp->dtype);
            for (size_t i = 0; i < len; i++) {
                req->data[i] = (uint8_t)(v >> (8u * i));
            }
            req->len = len;
            break;
        }
    }
}

void
canboss_sdo_init(void) {
    memset(host_store, 0, sizeof(host_store));
    printf("canboss_sdo: Host-Mock aktiv (simulierte Datenpunkte)\n");
}

bool
canboss_sdo_submit(canboss_sdo_req_t* req) {
    req->state = CB_SDO_PENDING;

    if (req->write) {
        host_entry_t* e = host_store_find(req->node_id, req->index, req->sub, true);
        if (e == NULL) {
            req->abort_code = 0x05040005; /* out of memory */
            req->state = CB_SDO_ERROR;
            return true;
        }
        memcpy(e->data, (const void*)req->data, req->len);
        e->len = req->len;
        req->state = CB_SDO_DONE;
        return true;
    }

    host_entry_t* e = host_store_find(req->node_id, req->index, req->sub, false);
    if (e != NULL) {
        memcpy((void*)req->data, e->data, e->len);
        req->len = e->len;
    } else {
        host_simulate(host_find_dp(req->node_id, req->index, req->sub), req->index, req->sub, req);
    }
    req->state = CB_SDO_DONE;
    return true;
}
