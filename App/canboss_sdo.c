/**
 * canboss_sdo.c
 *
 * SDO-Worker: sequenzielle Abarbeitung von Lese-/Schreibauftraegen ueber
 * den CANopenNode-SDO-Client. Blockierende Transfers laufen ausschliesslich
 * in diesem Task, die LVGL-Oberflaeche pollt nur das Statusfeld.
 */

#include "canboss_sdo.h"

#include "CANopen.h"
#include "CO_app_STM32.h"
#include "301/CO_SDOclient.h"

#include "FreeRTOS.h"
#include "cmsis_os2.h"

#define CB_SDO_QUEUE_LEN     16u
#define CB_SDO_TIMEOUT_MS    500u
#define CB_SDO_POLL_DELAY_MS 1u

static osMessageQueueId_t cb_sdo_queue;
static osThreadId_t cb_sdo_thread;

static const osThreadAttr_t cb_sdo_thread_attr = {
    .name = "canbossSDO",
    .priority = (osPriority_t)osPriorityBelowNormal,
    .stack_size = 2048,
};

/* Blockierender SDO-Upload (lesen), Muster aus 301/CO_SDOclient.h */
static CO_SDO_abortCode_t
cb_read_SDO(CO_SDOclient_t* SDO_C, uint8_t nodeId, uint16_t index, uint8_t subIndex, uint8_t* buf, size_t bufSize,
            size_t* readSize) {
    CO_SDO_return_t SDO_ret;

    SDO_ret = CO_SDOclient_setup(SDO_C, CO_CAN_ID_SDO_CLI + nodeId, CO_CAN_ID_SDO_SRV + nodeId, nodeId);
    if (SDO_ret != CO_SDO_RT_ok_communicationEnd) {
        return CO_SDO_AB_GENERAL;
    }

    SDO_ret = CO_SDOclientUploadInitiate(SDO_C, index, subIndex, CB_SDO_TIMEOUT_MS, false);
    if (SDO_ret != CO_SDO_RT_ok_communicationEnd) {
        return CO_SDO_AB_GENERAL;
    }

    do {
        uint32_t timeDifference_us = CB_SDO_POLL_DELAY_MS * 1000u;
        CO_SDO_abortCode_t abortCode = CO_SDO_AB_NONE;

        SDO_ret = CO_SDOclientUpload(SDO_C, timeDifference_us, false, &abortCode, NULL, NULL, NULL);
        if (SDO_ret < 0) {
            return abortCode;
        }
        osDelay(CB_SDO_POLL_DELAY_MS);
    } while (SDO_ret > 0);

    *readSize = CO_SDOclientUploadBufRead(SDO_C, buf, bufSize);
    return CO_SDO_AB_NONE;
}

/* Blockierender SDO-Download (schreiben), Muster aus 301/CO_SDOclient.h */
static CO_SDO_abortCode_t
cb_write_SDO(CO_SDOclient_t* SDO_C, uint8_t nodeId, uint16_t index, uint8_t subIndex, const uint8_t* data,
             size_t dataSize) {
    CO_SDO_return_t SDO_ret;
    bool_t bufferPartial = false;

    SDO_ret = CO_SDOclient_setup(SDO_C, CO_CAN_ID_SDO_CLI + nodeId, CO_CAN_ID_SDO_SRV + nodeId, nodeId);
    if (SDO_ret != CO_SDO_RT_ok_communicationEnd) {
        return CO_SDO_AB_GENERAL;
    }

    SDO_ret = CO_SDOclientDownloadInitiate(SDO_C, index, subIndex, dataSize, CB_SDO_TIMEOUT_MS, false);
    if (SDO_ret != CO_SDO_RT_ok_communicationEnd) {
        return CO_SDO_AB_GENERAL;
    }

    size_t nWritten = CO_SDOclientDownloadBufWrite(SDO_C, data, dataSize);
    if (nWritten < dataSize) {
        bufferPartial = true;
        /* Datenpunkte sind auf CANBOSS_DP_DATA_MAX begrenzt und passen in
         * den Clientpuffer; groessere Transfers werden nicht unterstuetzt. */
        return CO_SDO_AB_OUT_OF_MEM;
    }

    do {
        uint32_t timeDifference_us = CB_SDO_POLL_DELAY_MS * 1000u;
        CO_SDO_abortCode_t abortCode = CO_SDO_AB_NONE;

        SDO_ret = CO_SDOclientDownload(SDO_C, timeDifference_us, false, bufferPartial, &abortCode, NULL, NULL);
        if (SDO_ret < 0) {
            return abortCode;
        }
        osDelay(CB_SDO_POLL_DELAY_MS);
    } while (SDO_ret > 0);

    return CO_SDO_AB_NONE;
}

static void
cb_sdo_worker(void* argument) {
    (void)argument;
    canboss_sdo_req_t* req;

    for (;;) {
        if (osMessageQueueGet(cb_sdo_queue, &req, NULL, osWaitForever) != osOK) {
            continue;
        }

        CO_t* co = canopenNodeSTM32 != NULL ? canopenNodeSTM32->canOpenStack : NULL;
        if (co == NULL || co->SDOclient == NULL || co->nodeIdUnconfigured) {
            req->abort_code = CO_SDO_AB_GENERAL;
            req->state = CB_SDO_ERROR;
            continue;
        }

        CO_SDO_abortCode_t abort;
        if (req->write) {
            abort = cb_write_SDO(&co->SDOclient[0], req->node_id, req->index, req->sub, req->data, req->len);
        } else {
            size_t readSize = 0;
            abort = cb_read_SDO(&co->SDOclient[0], req->node_id, req->index, req->sub, req->data, sizeof(req->data),
                                &readSize);
            req->len = readSize;
        }

        if (abort == CO_SDO_AB_NONE) {
            req->state = CB_SDO_DONE;
        } else {
            req->abort_code = (uint32_t)abort;
            req->state = CB_SDO_ERROR;
        }
    }
}

void
canboss_sdo_init(void) {
    cb_sdo_queue = osMessageQueueNew(CB_SDO_QUEUE_LEN, sizeof(canboss_sdo_req_t*), NULL);
    cb_sdo_thread = osThreadNew(cb_sdo_worker, NULL, &cb_sdo_thread_attr);
    (void)cb_sdo_thread;
}

bool
canboss_sdo_submit(canboss_sdo_req_t* req) {
    if (cb_sdo_queue == NULL || req == NULL) {
        return false;
    }
    req->abort_code = 0;
    req->state = CB_SDO_PENDING;
    if (osMessageQueuePut(cb_sdo_queue, &req, 0, 0) != osOK) {
        req->state = CB_SDO_IDLE;
        return false;
    }
    return true;
}
