/**
 * canboss_sdo_zephyr.c
 *
 * Zephyr-Implementierung der asynchronen SDO-Client-Schicht
 * (App/canboss_sdo.h): die LVGL-UI stellt Auftraege in eine k_msgq,
 * ein Worker-Thread arbeitet sie sequenziell ueber die blockierenden
 * SDO-Transfers aus co_zephyr.c ab; die UI pollt nur das Statusfeld.
 */

#include "canboss_sdo.h"

#include "co_zephyr.h"
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
