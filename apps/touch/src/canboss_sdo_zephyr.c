/**
 * canboss_sdo_zephyr.c
 *
 * Zephyr-Implementierung der asynchronen SDO-Client-Schicht
 * (App/canboss_sdo.h): die LVGL-UI stellt Auftraege in eine k_msgq,
 * ein Worker-Thread arbeitet sie sequenziell ueber die blockierenden
 * SDO-Transfers aus lib/canopen/co_node.c ab; die UI pollt nur das Statusfeld.
 */

#include "canboss_sdo.h"

#include "co_node.h"
#include "osal.h"

#include <zephyr/kernel.h>

#define CB_SDO_QUEUE_LEN 16u

K_MSGQ_DEFINE(cb_sdo_msgq, sizeof(canboss_sdo_req_t*), CB_SDO_QUEUE_LEN, sizeof(void*));

static void
cb_sdo_worker(void* arg) {
    (void)arg;
    canboss_sdo_req_t* req;

    for (;;) {
        if (k_msgq_get(&cb_sdo_msgq, &req, K_FOREVER) != 0) {
            continue;
        }

#ifdef CB_DEBUG_FAKE_SDO
        /* Testpfad ohne Bus: zufaellige Erfolgs-/Fehlerantworten, damit
         * die UI-Ergebnispfade (cb_apply_read_result) durchlaufen werden */
        {
            static uint32_t seed = 0x51e9d3a7u;
            seed = seed * 1664525u + 1013904223u;
            k_msleep(1 + (int32_t)((seed >> 8) % 20u));
            if (((seed >> 16) % 8u) == 0u) {
                req->abort_code = 0x06020000u; /* Objekt nicht vorhanden */
                req->state = CB_SDO_ERROR;
            } else {
                if (!req->write) {
                    size_t n = 1u + (size_t)((seed >> 10) % sizeof(req->data));
                    for (size_t i = 0; i < n; i++) {
                        seed = seed * 1664525u + 1013904223u;
                        ((uint8_t*)req->data)[i] = (uint8_t)(seed >> 16);
                    }
                    req->len = n;
                }
                req->state = CB_SDO_DONE;
            }
            continue;
        }
#endif

        uint32_t abort = 0;
        if (req->write) {
            if (cb_co_sdo_write(req->node_id, req->index, req->sub, (const uint8_t*)req->data, req->len, &abort)
                == 0) {
                req->state = CB_SDO_DONE;
            } else {
                req->abort_code = abort;
                req->state = CB_SDO_ERROR;
            }
        } else {
            size_t got = 0;
            if (cb_co_sdo_read(req->node_id, req->index, req->sub, (uint8_t*)req->data, sizeof(req->data), &got,
                               &abort)
                == 0) {
                req->len = got;
                req->state = CB_SDO_DONE;
            } else {
                req->abort_code = abort;
                req->state = CB_SDO_ERROR;
            }
        }
    }
}

void
canboss_sdo_init(void) {
    (void)cb_thread_start(CB_THREAD_SDO_WORKER, cb_sdo_worker, NULL);
}

bool
canboss_sdo_submit(canboss_sdo_req_t* req) {
    if (req == NULL) {
        return false;
    }
    req->abort_code = 0;
    req->state = CB_SDO_PENDING;
    if (k_msgq_put(&cb_sdo_msgq, &req, K_NO_WAIT) != 0) {
        req->state = CB_SDO_IDLE;
        return false;
    }
    return true;
}
