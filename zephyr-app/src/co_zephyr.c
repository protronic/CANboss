/**
 * co_zephyr.c
 *
 * CANopenNode-Stack-Anbindung der Zephyr-App: Initialisierung nach dem
 * Muster von CANopenNode/example/main_blank.c mit dem generierten
 * Objektverzeichnis App/OD (canboss_master.eds, Standard-OD-Praefix),
 * dazu zwei Threads:
 *
 *   - RX-Thread:       Backend-recv() -> CO_CANrxDispatch()
 *                      (+ Raw-Frame-Hook fuer die PoC-Hallenlichtsteuerung)
 *   - Mainline-Thread: CO_process() + SYNC/RPDO/TPDO im 1-ms-Raster
 *
 * Die blockierenden SDO-Transfers laufen im Aufruferthread (SDO-Worker
 * der LVGL-UI bzw. Shell-Thread bei Berry) und sind ueber CB_MUTEX_SDO
 * serialisiert - dasselbe Muster wie App/canboss_sdo.c am STM32.
 */

#include "co_zephyr.h"
#include "can_if.h"
#include "osal.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "CANopen.h"
#include "301/CO_SDOclient.h"
#include "OD.h"

#include "canboss_poc.h"

#define CB_CO_MAINLINE_INTERVAL_US 1000u
#define CB_CO_RX_TIMEOUT_MS        1
#define CB_CO_FIRST_HB_TIME_MS     500u
#define CB_CO_SDO_SRV_TIMEOUT_MS   1000u

static CO_t* cb_co = NULL;
static const cb_can_backend_t* cb_backend = NULL;
static volatile bool cb_running = false;
static char cb_error_text[256];

/* RX-Thread: Frames vom Backend an den Stack verteilen */
static void
cb_rx_loop(void* arg) {
    (void)arg;
    cb_can_frame_t frame;
    CO_CANrxMsg_t msg;

    while (cb_running) {
        int ret = cb_backend->recv(&frame, CB_CO_RX_TIMEOUT_MS);
        if (ret < 0) {
            cb_sleep_us(10000);
            continue;
        }
        if (ret == 0 || frame.rtr) {
            continue;
        }
        msg.ident = frame.id;
        msg.DLC = frame.dlc;
        memcpy(msg.data, frame.data, sizeof(msg.data));
        CO_CANrxDispatch(cb_co->CANmodule, &msg);

        /* Raw-Frames der PoC-Hallenlichtsteuerung (filtert selbst
         * auf die minp-ID) */
        canboss_poc_can_rx(frame.id, frame.data, frame.dlc);
    }
}

/* Mainline-Thread: zyklische Stack-Verarbeitung */
static void
cb_mainline_loop(void* arg) {
    (void)arg;
    uint64_t last = cb_now_us();

    while (cb_running) {
        cb_sleep_us(CB_CO_MAINLINE_INTERVAL_US);
        uint64_t now = cb_now_us();
        uint32_t diff_us = (uint32_t)(now - last);
        last = now;

        /* NMT-Reset-Kommandos vom Bus ignoriert das Panel bewusst */
        (void)CO_process(cb_co, false, diff_us, NULL);

        CO_LOCK_OD(cb_co->CANmodule);
        if (!cb_co->nodeIdUnconfigured && cb_co->CANmodule->CANnormal) {
            bool_t syncWas = false;
#if (CO_CONFIG_SYNC) & CO_CONFIG_SYNC_ENABLE
            syncWas = CO_process_SYNC(cb_co, diff_us, NULL);
#endif
#if (CO_CONFIG_PDO) & CO_CONFIG_RPDO_ENABLE
            CO_process_RPDO(cb_co, syncWas, diff_us, NULL);
#endif
#if (CO_CONFIG_PDO) & CO_CONFIG_TPDO_ENABLE
            CO_process_TPDO(cb_co, syncWas, diff_us, NULL);
#endif
        }
        CO_UNLOCK_OD(cb_co->CANmodule);
    }
}

static int
cb_fail(const char* fmt, int code) {
    snprintf(cb_error_text, sizeof(cb_error_text), fmt, code);
    return -1;
}

int
cb_co_start(uint8_t own_node_id) {
    CO_ReturnError_t err;
    uint32_t errInfo = 0;
    uint32_t heapUsed = 0;

    cb_error_text[0] = '\0';

    const cb_can_backend_t* backend = cb_can_backend_find("zephyr");
    if (backend == NULL) {
        snprintf(cb_error_text, sizeof(cb_error_text), "CAN-Backend 'zephyr' fehlt");
        return -1;
    }

    if (backend->open("zephyr,canbus", 0) != 0) {
        snprintf(cb_error_text, sizeof(cb_error_text), "CAN oeffnen fehlgeschlagen: %s", strerror(errno));
        return -1;
    }
    cb_backend = backend;

    cb_co = CO_new(NULL /* Zaehler aus OD.h */, &heapUsed);
    if (cb_co == NULL) {
        backend->close();
        snprintf(cb_error_text, sizeof(cb_error_text), "CO_new: keine Speicherzuteilung");
        return -1;
    }

    err = CO_CANinit(cb_co, (void*)(uintptr_t)backend, 0 /* Bitrate macht das Backend/DT */);
    if (err != CO_ERROR_NO) {
        CO_delete(cb_co);
        cb_co = NULL;
        backend->close();
        return cb_fail("CO_CANinit fehlgeschlagen: %d", (int)err);
    }

    err = CO_CANopenInit(cb_co, NULL, NULL, OD, NULL,
                         CO_NMT_STARTUP_TO_OPERATIONAL | CO_NMT_ERR_ON_ERR_REG | CO_ERR_REG_GENERIC_ERR
                             | CO_ERR_REG_COMMUNICATION,
                         CB_CO_FIRST_HB_TIME_MS, CB_CO_SDO_SRV_TIMEOUT_MS, CB_CO_SDO_TIMEOUT_MS, false, own_node_id,
                         &errInfo);
    if (err != CO_ERROR_NO) {
        CO_delete(cb_co);
        cb_co = NULL;
        backend->close();
        if (err == CO_ERROR_OD_PARAMETERS) {
            return cb_fail("CO_CANopenInit: OD-Eintrag 0x%X fehlerhaft", (int)errInfo);
        }
        return cb_fail("CO_CANopenInit fehlgeschlagen: %d", (int)err);
    }

    err = CO_CANopenInitPDO(cb_co, cb_co->em, OD, own_node_id, &errInfo);
    if (err != CO_ERROR_NO) {
        CO_delete(cb_co);
        cb_co = NULL;
        backend->close();
        return cb_fail("CO_CANopenInitPDO fehlgeschlagen: %d", (int)err);
    }

    CO_CANsetNormalMode(cb_co->CANmodule);

    cb_running = true;
    if (cb_thread_start(CB_THREAD_CAN_RX, cb_rx_loop, NULL) != 0
        || cb_thread_start(CB_THREAD_MAINLINE, cb_mainline_loop, NULL) != 0) {
        cb_running = false;
        CO_delete(cb_co);
        cb_co = NULL;
        backend->close();
        snprintf(cb_error_text, sizeof(cb_error_text), "Threads starten fehlgeschlagen");
        return -1;
    }

    return 0;
}

void
cb_co_stop(void) {
    if (!cb_running) {
        return;
    }
    cb_running = false;
    cb_thread_join(CB_THREAD_CAN_RX);
    cb_thread_join(CB_THREAD_MAINLINE);
    CO_CANmodule_disable(cb_co->CANmodule);
    CO_delete(cb_co);
    cb_co = NULL;
    if (cb_backend != NULL) {
        cb_backend->close();
        cb_backend = NULL;
    }
}

bool
cb_co_connected(void) {
    return cb_running && cb_co != NULL && !cb_co->nodeIdUnconfigured;
}

const char*
cb_co_error(void) {
    return cb_error_text;
}

void*
cb_co_handle(void) {
    return cb_co;
}

/* Blockierender SDO-Upload, Muster aus 301/CO_SDOclient.h bzw.
 * App/canboss_sdo.c */
static CO_SDO_abortCode_t
cb_read_SDO(CO_SDOclient_t* SDO_C, uint8_t nodeId, uint16_t index, uint8_t subIndex, uint8_t* buf, size_t bufSize,
            size_t* readSize) {
    CO_SDO_return_t SDO_ret;

    SDO_ret = CO_SDOclient_setup(SDO_C, CO_CAN_ID_SDO_CLI + nodeId, CO_CAN_ID_SDO_SRV + nodeId, nodeId);
    if (SDO_ret != CO_SDO_RT_ok_communicationEnd) {
        return CO_SDO_AB_GENERAL;
    }

    SDO_ret = CO_SDOclientUploadInitiate(SDO_C, index, subIndex, CB_CO_SDO_TIMEOUT_MS, false);
    if (SDO_ret != CO_SDO_RT_ok_communicationEnd) {
        return CO_SDO_AB_GENERAL;
    }

    do {
        uint32_t timeDifference_us = 1000;
        CO_SDO_abortCode_t abortCode = CO_SDO_AB_NONE;

        SDO_ret = CO_SDOclientUpload(SDO_C, timeDifference_us, false, &abortCode, NULL, NULL, NULL);
        if (SDO_ret < 0) {
            return abortCode;
        }
        cb_sleep_us(1000);
    } while (SDO_ret > 0);

    *readSize = CO_SDOclientUploadBufRead(SDO_C, buf, bufSize);
    return CO_SDO_AB_NONE;
}

static CO_SDO_abortCode_t
cb_write_SDO(CO_SDOclient_t* SDO_C, uint8_t nodeId, uint16_t index, uint8_t subIndex, const uint8_t* data,
             size_t dataSize) {
    CO_SDO_return_t SDO_ret;

    SDO_ret = CO_SDOclient_setup(SDO_C, CO_CAN_ID_SDO_CLI + nodeId, CO_CAN_ID_SDO_SRV + nodeId, nodeId);
    if (SDO_ret != CO_SDO_RT_ok_communicationEnd) {
        return CO_SDO_AB_GENERAL;
    }

    SDO_ret = CO_SDOclientDownloadInitiate(SDO_C, index, subIndex, dataSize, CB_CO_SDO_TIMEOUT_MS, false);
    if (SDO_ret != CO_SDO_RT_ok_communicationEnd) {
        return CO_SDO_AB_GENERAL;
    }

    if (CO_SDOclientDownloadBufWrite(SDO_C, data, dataSize) < dataSize) {
        return CO_SDO_AB_OUT_OF_MEM;
    }

    do {
        uint32_t timeDifference_us = 1000;
        CO_SDO_abortCode_t abortCode = CO_SDO_AB_NONE;

        SDO_ret = CO_SDOclientDownload(SDO_C, timeDifference_us, false, false, &abortCode, NULL, NULL);
        if (SDO_ret < 0) {
            return abortCode;
        }
        cb_sleep_us(1000);
    } while (SDO_ret > 0);

    return CO_SDO_AB_NONE;
}

int
cb_co_sdo_read(uint8_t node_id, uint16_t index, uint8_t sub, uint8_t* buf, size_t buf_size, size_t* read_size,
               uint32_t* abort_code) {
    if (!cb_co_connected() || cb_co->SDOclient == NULL) {
        *abort_code = 0;
        return -1;
    }

    cb_mutex_lock(CB_MUTEX_SDO);
    CO_SDO_abortCode_t abort = cb_read_SDO(&cb_co->SDOclient[0], node_id, index, sub, buf, buf_size, read_size);
    CO_SDOclientClose(&cb_co->SDOclient[0]);
    cb_mutex_unlock(CB_MUTEX_SDO);

    if (abort != CO_SDO_AB_NONE) {
        *abort_code = (uint32_t)abort;
        return -1;
    }
    return 0;
}

int
cb_co_sdo_write(uint8_t node_id, uint16_t index, uint8_t sub, const uint8_t* data, size_t len, uint32_t* abort_code) {
    if (!cb_co_connected() || cb_co->SDOclient == NULL) {
        *abort_code = 0;
        return -1;
    }

    cb_mutex_lock(CB_MUTEX_SDO);
    CO_SDO_abortCode_t abort = cb_write_SDO(&cb_co->SDOclient[0], node_id, index, sub, data, len);
    CO_SDOclientClose(&cb_co->SDOclient[0]);
    cb_mutex_unlock(CB_MUTEX_SDO);

    if (abort != CO_SDO_AB_NONE) {
        *abort_code = (uint32_t)abort;
        return -1;
    }
    return 0;
}

const char*
cb_co_abort_str(uint32_t abort_code) {
    switch (abort_code) {
        case 0: return "Timeout/lokaler Fehler";
        case 0x05030000: return "Toggle-Bit unveraendert";
        case 0x05040000: return "SDO-Timeout";
        case 0x05040001: return "Ungueltiges Kommando";
        case 0x06010000: return "Zugriff nicht unterstuetzt";
        case 0x06010001: return "Objekt nicht lesbar";
        case 0x06010002: return "Objekt nicht schreibbar";
        case 0x06020000: return "Objekt nicht vorhanden";
        case 0x06070010: return "Datenlaenge passt nicht";
        case 0x06090011: return "Subindex nicht vorhanden";
        case 0x06090030: return "Wertebereich verletzt";
        case 0x08000000: return "Allgemeiner Fehler";
        default: {
            static char text[32];
            snprintf(text, sizeof(text), "SDO-Abort 0x%08X", abort_code);
            return text;
        }
    }
}
