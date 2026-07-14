/**
 * co_node.c
 *
 * CANopenNode-Stack-Anbindung: Initialisierung nach dem Muster von
 * CANopenNode/example/main_blank.c (CO_MULTIPLE_OD mit dem generierten
 * Objektverzeichnis od/canboss_master.h), dazu zwei Threads:
 *
 *   - RX-Thread:       Backend-recv() -> CO_CANrxDispatch()
 *   - Mainline-Thread: CO_process() + SYNC/RPDO/TPDO im 1-ms-Raster
 *
 * Die blockierenden SDO-Transfers (cb_co_sdo_read/_write) laufen im
 * Aufruferthread (UI) und sind untereinander per Mutex serialisiert -
 * dasselbe Muster wie der SDO-Worker in CANbossTouch/App/canboss_sdo.c.
 */

#include "co_node.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "CANopen.h"
#include "301/CO_SDOclient.h"
#include "canboss_master.h"

#define CB_CO_MAINLINE_INTERVAL_US 1000u
#define CB_CO_RX_TIMEOUT_MS        1
#define CB_CO_FIRST_HB_TIME_MS     500u
#define CB_CO_SDO_SRV_TIMEOUT_MS   1000u

static CO_t* cb_co = NULL;
static const cb_can_backend_t* cb_backend = NULL;
static volatile bool cb_running = false;
static pthread_t cb_rx_thread;
static pthread_t cb_mainline_thread;
static pthread_mutex_t cb_sdo_mutex = PTHREAD_MUTEX_INITIALIZER;
static char cb_error_text[256];

static uint64_t
monotonic_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

/* RX-Thread: Frames vom Backend an den Stack verteilen */
static void*
cb_rx_loop(void* arg) {
    (void)arg;
    cb_can_frame_t frame;
    CO_CANrxMsg_t msg;

    while (cb_running) {
        int ret = cb_backend->recv(&frame, CB_CO_RX_TIMEOUT_MS);
        if (ret < 0) {
            /* Backend weg (Interface down o.ae.): kurz warten */
            usleep(10000);
            continue;
        }
        if (ret == 0 || frame.rtr) {
            continue;
        }
        msg.ident = frame.id;
        msg.DLC = frame.dlc;
        memcpy(msg.data, frame.data, sizeof(msg.data));
        CO_CANrxDispatch(cb_co->CANmodule, &msg);
    }
    return NULL;
}

/* Mainline-Thread: zyklische Stack-Verarbeitung */
static void*
cb_mainline_loop(void* arg) {
    (void)arg;
    uint64_t last = monotonic_us();

    while (cb_running) {
        usleep(CB_CO_MAINLINE_INTERVAL_US);
        uint64_t now = monotonic_us();
        uint32_t diff_us = (uint32_t)(now - last);
        last = now;

        /* NMT-Reset-Kommandos vom Bus ignoriert der Monitor bewusst:
         * er ist Beobachter und startet nicht neu. */
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
    return NULL;
}

static int
cb_fail(const char* fmt, int code) {
    snprintf(cb_error_text, sizeof(cb_error_text), fmt, code);
    return -1;
}

int
cb_co_start(const cb_can_backend_t* backend, const char* device, uint32_t bitrate, uint8_t own_node_id) {
    CO_ReturnError_t err;
    uint32_t errInfo = 0;
    uint32_t heapUsed = 0;

    cb_error_text[0] = '\0';

    if (backend == NULL || device == NULL) {
        snprintf(cb_error_text, sizeof(cb_error_text), "kein CAN-Backend/Geraet angegeben");
        return -1;
    }

    if (backend->open(device, bitrate) != 0) {
        snprintf(cb_error_text, sizeof(cb_error_text), "%s '%s' oeffnen fehlgeschlagen: %s", backend->name, device,
                 strerror(errno));
        return -1;
    }
    cb_backend = backend;

    /* OD-Konfiguration aus dem generierten canboss_master-Verzeichnis */
    static CO_config_t co_config;
    memset(&co_config, 0, sizeof(co_config));
    canboss_master_INIT_CONFIG(co_config);

    cb_co = CO_new(&co_config, &heapUsed);
    if (cb_co == NULL) {
        backend->close();
        snprintf(cb_error_text, sizeof(cb_error_text), "CO_new: keine Speicherzuteilung");
        return -1;
    }

    err = CO_CANinit(cb_co, (void*)(uintptr_t)backend, 0 /* Bitrate macht das Backend */);
    if (err != CO_ERROR_NO) {
        CO_delete(cb_co);
        cb_co = NULL;
        backend->close();
        return cb_fail("CO_CANinit fehlgeschlagen: %d", (int)err);
    }

    err = CO_CANopenInit(cb_co, NULL, NULL, canboss_master, NULL,
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

    err = CO_CANopenInitPDO(cb_co, cb_co->em, canboss_master, own_node_id, &errInfo);
    if (err != CO_ERROR_NO) {
        CO_delete(cb_co);
        cb_co = NULL;
        backend->close();
        return cb_fail("CO_CANopenInitPDO fehlgeschlagen: %d", (int)err);
    }

    CO_CANsetNormalMode(cb_co->CANmodule);

    cb_running = true;
    if (pthread_create(&cb_rx_thread, NULL, cb_rx_loop, NULL) != 0
        || pthread_create(&cb_mainline_thread, NULL, cb_mainline_loop, NULL) != 0) {
        cb_running = false;
        CO_delete(cb_co);
        cb_co = NULL;
        backend->close();
        snprintf(cb_error_text, sizeof(cb_error_text), "Threads starten fehlgeschlagen: %s", strerror(errno));
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
    pthread_join(cb_rx_thread, NULL);
    pthread_join(cb_mainline_thread, NULL);
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

/* Blockierender SDO-Upload, Muster aus 301/CO_SDOclient.h bzw.
 * CANbossTouch/App/canboss_sdo.c */
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
        usleep(1000);
    } while (SDO_ret > 0);

    *readSize = CO_SDOclientUploadBufRead(SDO_C, buf, bufSize);
    return CO_SDO_AB_NONE;
}

/* Blockierender SDO-Download, Muster wie oben */
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
        /* Datenpunkte sind auf CB_DP_DATA_MAX begrenzt und passen in den
         * Clientpuffer; groessere Transfers werden nicht unterstuetzt. */
        return CO_SDO_AB_OUT_OF_MEM;
    }

    do {
        uint32_t timeDifference_us = 1000;
        CO_SDO_abortCode_t abortCode = CO_SDO_AB_NONE;

        SDO_ret = CO_SDOclientDownload(SDO_C, timeDifference_us, false, false, &abortCode, NULL, NULL);
        if (SDO_ret < 0) {
            return abortCode;
        }
        usleep(1000);
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

    pthread_mutex_lock(&cb_sdo_mutex);
    CO_SDO_abortCode_t abort = cb_read_SDO(&cb_co->SDOclient[0], node_id, index, sub, buf, buf_size, read_size);
    CO_SDOclientClose(&cb_co->SDOclient[0]);
    pthread_mutex_unlock(&cb_sdo_mutex);

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

    pthread_mutex_lock(&cb_sdo_mutex);
    CO_SDO_abortCode_t abort = cb_write_SDO(&cb_co->SDOclient[0], node_id, index, sub, data, len);
    CO_SDOclientClose(&cb_co->SDOclient[0]);
    pthread_mutex_unlock(&cb_sdo_mutex);

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
        case 0x06070012: return "Datenlaenge zu gross";
        case 0x06070013: return "Datenlaenge zu klein";
        case 0x06090011: return "Subindex nicht vorhanden";
        case 0x06090030: return "Wertebereich verletzt";
        case 0x06090031: return "Wert zu gross";
        case 0x06090032: return "Wert zu klein";
        case 0x08000000: return "Allgemeiner Fehler";
        case 0x08000020: return "Datentransfer nicht moeglich";
        case 0x08000022: return "Geraetezustand verhindert Zugriff";
        default: {
            static char text[32];
            snprintf(text, sizeof(text), "SDO-Abort 0x%08X", abort_code);
            return text;
        }
    }
}
