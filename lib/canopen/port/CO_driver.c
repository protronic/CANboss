/**
 * CO_driver.c
 *
 * CANopenNode-Treiber auf Basis der CANboss-Backend-Abstraktion
 * (src/can_if.h). Struktur nach CANopenNode/example/CO_driver_blank.c;
 * gesendet wird direkt ueber das Backend (SocketCAN heute, seriell
 * spaeter), empfangen ueber CO_CANrxDispatch() aus dem RX-Thread in
 * src/co_node.c. Busfehler (TEC/REC) kommen aus backend->get_errors()
 * und werden in CO_CANmodule_process() wie im Blank-Treiber auf
 * CANerrorStatus abgebildet.
 */

#include "301/CO_driver.h"

#include "can_if.h"
#include "osal.h"

#include <string.h>

void
CO_driver_lock_send(void) {
    cb_mutex_lock(CB_MUTEX_CAN_SEND);
}

void
CO_driver_unlock_send(void) {
    cb_mutex_unlock(CB_MUTEX_CAN_SEND);
}

void
CO_driver_lock_emcy(void) {
    cb_mutex_lock(CB_MUTEX_EMCY);
}

void
CO_driver_unlock_emcy(void) {
    cb_mutex_unlock(CB_MUTEX_EMCY);
}

void
CO_driver_lock_od(void) {
    cb_mutex_lock(CB_MUTEX_OD);
}

void
CO_driver_unlock_od(void) {
    cb_mutex_unlock(CB_MUTEX_OD);
}

void
CO_CANsetConfigurationMode(void* CANptr) {
    (void)CANptr; /* Backend wird von der Anwendung geoeffnet/geschlossen */
}

void
CO_CANsetNormalMode(CO_CANmodule_t* CANmodule) {
    CANmodule->CANnormal = true;
}

CO_ReturnError_t
CO_CANmodule_init(CO_CANmodule_t* CANmodule, void* CANptr, CO_CANrx_t rxArray[], uint16_t rxSize, CO_CANtx_t txArray[],
                  uint16_t txSize, uint16_t CANbitRate) {
    uint16_t i;

    (void)CANbitRate; /* Bitrate konfiguriert das Backend bzw. ip link */

    if (CANmodule == NULL || rxArray == NULL || txArray == NULL) {
        return CO_ERROR_ILLEGAL_ARGUMENT;
    }

    CANmodule->CANptr = CANptr;
    CANmodule->rxArray = rxArray;
    CANmodule->rxSize = rxSize;
    CANmodule->txArray = txArray;
    CANmodule->txSize = txSize;
    CANmodule->CANerrorStatus = 0;
    CANmodule->CANnormal = false;
    CANmodule->useCANrxFilters = false; /* Software-Filter in CO_CANrxDispatch */
    CANmodule->bufferInhibitFlag = false;
    CANmodule->firstCANtxMessage = true;
    CANmodule->CANtxCount = 0U;
    CANmodule->errOld = 0U;

    for (i = 0U; i < rxSize; i++) {
        rxArray[i].ident = 0U;
        rxArray[i].mask = 0xFFFFU;
        rxArray[i].object = NULL;
        rxArray[i].CANrx_callback = NULL;
    }
    for (i = 0U; i < txSize; i++) {
        txArray[i].bufferFull = false;
    }

    return CO_ERROR_NO;
}

void
CO_CANmodule_disable(CO_CANmodule_t* CANmodule) {
    if (CANmodule != NULL) {
        CANmodule->CANnormal = false;
    }
}

CO_ReturnError_t
CO_CANrxBufferInit(CO_CANmodule_t* CANmodule, uint16_t index, uint16_t ident, uint16_t mask, bool_t rtr, void* object,
                   void (*CANrx_callback)(void* object, void* message)) {
    CO_ReturnError_t ret = CO_ERROR_NO;

    if ((CANmodule != NULL) && (object != NULL) && (CANrx_callback != NULL) && (index < CANmodule->rxSize)) {
        CO_CANrx_t* buffer = &CANmodule->rxArray[index];

        buffer->object = object;
        buffer->CANrx_callback = CANrx_callback;

        /* Bit 11 codiert RTR, wie im Blank-Treiber */
        buffer->ident = ident & 0x07FFU;
        if (rtr) {
            buffer->ident |= 0x0800U;
        }
        buffer->mask = (mask & 0x07FFU) | 0x0800U;
    } else {
        ret = CO_ERROR_ILLEGAL_ARGUMENT;
    }

    return ret;
}

CO_CANtx_t*
CO_CANtxBufferInit(CO_CANmodule_t* CANmodule, uint16_t index, uint16_t ident, bool_t rtr, uint8_t noOfBytes,
                   bool_t syncFlag) {
    CO_CANtx_t* buffer = NULL;

    if ((CANmodule != NULL) && (index < CANmodule->txSize)) {
        buffer = &CANmodule->txArray[index];

        buffer->ident = ((uint32_t)ident & 0x07FFU) | ((uint32_t)(rtr ? 0x8000U : 0U));
        buffer->DLC = noOfBytes > 8U ? 8U : noOfBytes;
        buffer->bufferFull = false;
        buffer->syncFlag = syncFlag;
    }

    return buffer;
}

/* Frame direkt ueber das Backend senden (blockiert hoechstens kurz) */
static CO_ReturnError_t
cb_driver_send(CO_CANmodule_t* CANmodule, CO_CANtx_t* buffer) {
    const cb_can_backend_t* backend = (const cb_can_backend_t*)CANmodule->CANptr;
    cb_can_frame_t frame;

    if (backend == NULL) {
        return CO_ERROR_ILLEGAL_ARGUMENT;
    }

    frame.id = (uint16_t)(buffer->ident & 0x07FFU);
    frame.rtr = (buffer->ident & 0x8000U) != 0U;
    frame.dlc = buffer->DLC;
    memcpy(frame.data, buffer->data, sizeof(frame.data));

    if (backend->send(&frame) != 0) {
        CANmodule->CANerrorStatus |= CO_CAN_ERRTX_OVERFLOW;
        return CO_ERROR_TX_OVERFLOW;
    }

    CANmodule->firstCANtxMessage = false;
    return CO_ERROR_NO;
}

CO_ReturnError_t
CO_CANsend(CO_CANmodule_t* CANmodule, CO_CANtx_t* buffer) {
    CO_ReturnError_t err;

    CO_LOCK_CAN_SEND(CANmodule);
    err = cb_driver_send(CANmodule, buffer);
    CO_UNLOCK_CAN_SEND(CANmodule);

    return err;
}

void
CO_CANclearPendingSyncPDOs(CO_CANmodule_t* CANmodule) {
    /* Es wird synchron gesendet, ohne HW-Puffer: nichts zu verwerfen. */
    (void)CANmodule;
}

void
CO_CANmodule_process(CO_CANmodule_t* CANmodule) {
    const cb_can_backend_t* backend;
    cb_can_err_t counters;
    uint32_t err;
    uint16_t txErrors;
    uint16_t rxErrors;
    uint16_t overflow;

    if (CANmodule == NULL) {
        return;
    }

    backend = (const cb_can_backend_t*)CANmodule->CANptr;
    if (backend == NULL || backend->get_errors == NULL || backend->get_errors(&counters) != 0) {
        return;
    }

    txErrors = counters.tx_errors;
    rxErrors = counters.rx_errors;
    overflow = counters.overflow;
    err = ((uint32_t)txErrors << 16) | ((uint32_t)rxErrors << 8) | (uint32_t)(overflow & 0xFFU);

    /* Abbildung wie CANopenNode/example/CO_driver_blank.c */
    if (CANmodule->errOld != err) {
        uint16_t status = CANmodule->CANerrorStatus;

        CANmodule->errOld = err;

        if (txErrors >= 256U) {
            status |= CO_CAN_ERRTX_BUS_OFF;
        } else {
            status &= 0xFFFF
                      ^ (CO_CAN_ERRTX_BUS_OFF | CO_CAN_ERRRX_WARNING | CO_CAN_ERRRX_PASSIVE | CO_CAN_ERRTX_WARNING
                         | CO_CAN_ERRTX_PASSIVE);

            if (rxErrors >= 128U) {
                status |= CO_CAN_ERRRX_WARNING | CO_CAN_ERRRX_PASSIVE;
            } else if (rxErrors >= 96U) {
                status |= CO_CAN_ERRRX_WARNING;
            }

            if (txErrors >= 128U) {
                status |= CO_CAN_ERRTX_WARNING | CO_CAN_ERRTX_PASSIVE;
            } else if (txErrors >= 96U) {
                status |= CO_CAN_ERRTX_WARNING;
            }

            if ((status & CO_CAN_ERRTX_PASSIVE) == 0) {
                status &= 0xFFFF ^ CO_CAN_ERRTX_OVERFLOW;
            }
        }

        if (overflow != 0U) {
            status |= CO_CAN_ERRRX_OVERFLOW;
        }

        CANmodule->CANerrorStatus = status;
    }
}

void
CO_CANrxDispatch(CO_CANmodule_t* CANmodule, const CO_CANrxMsg_t* rcvMsg) {
    CO_CANrx_t* buffer;
    uint16_t index;

    if (CANmodule == NULL || rcvMsg == NULL || !CANmodule->CANnormal) {
        return;
    }

    /* Software-Filter: rxArray nach passender CAN-ID durchsuchen
     * (wie CO_CANinterrupt im Blank-Treiber). */
    buffer = &CANmodule->rxArray[0];
    for (index = CANmodule->rxSize; index > 0U; index--) {
        if (((rcvMsg->ident ^ buffer->ident) & buffer->mask) == 0U) {
            if (buffer->CANrx_callback != NULL) {
                buffer->CANrx_callback(buffer->object, (void*)rcvMsg);
            }
            break;
        }
        buffer++;
    }
}
