/**
 * co_node.c
 *
 * Gemeinsame CANopenNode-Stack-Anbindung (siehe co_node.h):
 * Initialisierung nach dem Muster von CANopenNode/example/main_blank.c
 * (CO_MULTIPLE_OD, OD und CO_config_t kommen von der App), dazu zwei
 * Threads:
 *
 *   - RX-Thread:       Backend-recv() -> CO_CANrxDispatch()
 *                      (+ optionaler Raw-Frame-Hook, z.B. PoC-Hallenlicht)
 *   - Mainline-Thread: CO_process() + SYNC/RPDO/TPDO im 1-ms-Raster
 *
 * Die blockierenden SDO-Transfers (cb_co_sdo_read/_write) laufen im
 * Aufruferthread und sind untereinander per Mutex serialisiert.
 */

#include "co_node.h"
#include "osal.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "301/CO_SDOclient.h"

#if ((CO_CONFIG_GTW)&CO_CONFIG_GTW_ASCII) != 0
#include "309/CO_gateway_ascii.h"
#endif

#define CB_CO_MAINLINE_INTERVAL_US 1000u
#define CB_CO_RX_TIMEOUT_MS        1
#define CB_CO_FIRST_HB_TIME_MS     500u
#define CB_CO_SDO_SRV_TIMEOUT_MS   1000u

#ifndef NMT_CONTROL
#define NMT_CONTROL                                                                                                    \
    (CO_NMT_STARTUP_TO_OPERATIONAL | CO_NMT_ERR_ON_ERR_REG | CO_ERR_REG_GENERIC_ERR | CO_ERR_REG_COMMUNICATION)
#endif

static CO_t* cb_co = NULL;
static const cb_can_backend_t* cb_backend = NULL;
static volatile bool cb_running = false;
static char cb_error_text[256];
static cb_co_raw_rx_hook_t cb_raw_rx_hook = NULL;

#if ((CO_CONFIG_GTW)&CO_CONFIG_GTW_ASCII) != 0
static cb_co_gtwa_read_t cb_gtwa_read_cb = NULL;
static void* cb_gtwa_read_user = NULL;
static volatile bool cb_gtwa_enabled = false;

/* Signatur-Adapter CANopenNode -> cb_co_gtwa_read_t (Mainline-Thread) */
static size_t
cb_gtwa_read_adapter(void* object, const char* buf, size_t count, uint8_t* connectionOK) {
    (void)object;
    *connectionOK = 1;
    if (cb_gtwa_read_cb == NULL) {
        return count; /* keine Senke: Ausgabe verwerfen */
    }
    return cb_gtwa_read_cb(cb_gtwa_read_user, buf, count);
}
#endif /* CO_CONFIG_GTW_ASCII */

void
cb_co_set_raw_rx_hook(cb_co_raw_rx_hook_t hook) {
    cb_raw_rx_hook = hook;
}

int
cb_co_raw_send(uint16_t ident, const uint8_t* data, uint8_t dlc, bool rtr) {
    if (!cb_running || cb_backend == NULL) {
        return -ENODEV;
    }

    cb_can_frame_t frame = {.id = (uint16_t)(ident & 0x7FFu), .dlc = dlc > 8 ? 8 : dlc, .rtr = rtr};

    if (!rtr) {
        memcpy(frame.data, data, frame.dlc);
    }
    return (cb_backend->send(&frame) == 0) ? 0 : -EIO;
}

/* RX-Thread: Frames vom Backend an den Stack verteilen */
static void
cb_rx_loop(void* arg) {
    (void)arg;
    cb_can_frame_t frame;
    CO_CANrxMsg_t msg;

    while (cb_running) {
        int ret = cb_backend->recv(&frame, CB_CO_RX_TIMEOUT_MS);
        if (ret < 0) {
            /* Backend weg (Interface down o.ae.): kurz warten */
            cb_sleep_us(10000);
            continue;
        }
        if (ret == 0) {
            continue;
        }
        if (cb_raw_rx_hook != NULL) {
            cb_raw_rx_hook(frame.id, frame.data, frame.dlc, frame.rtr);
        }
        if (frame.rtr) {
            continue; /* CANopen (CiA 301) nutzt keine RTR-Frames */
        }
        msg.ident = frame.id;
        msg.DLC = frame.dlc;
        memcpy(msg.data, frame.data, sizeof(msg.data));
        CO_CANrxDispatch(cb_co->CANmodule, &msg);
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

        /* NMT-Reset-Kommandos vom Bus werden bewusst ignoriert
         * (Monitor/Panel beobachten nur, Demo-Knoten bleiben simpel). */
#if ((CO_CONFIG_GTW)&CO_CONFIG_GTW_ASCII) != 0
        (void)CO_process(cb_co, cb_gtwa_enabled, diff_us, NULL);
#else
        (void)CO_process(cb_co, false, diff_us, NULL);
#endif

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
cb_co_start(OD_t* od, const CO_config_t* config, const cb_can_backend_t* backend, const char* device,
            uint32_t bitrate, uint8_t own_node_id) {
    CO_ReturnError_t err;
    uint32_t errInfo = 0;
    uint32_t heapUsed = 0;

    cb_error_text[0] = '\0';

    if (od == NULL || config == NULL || backend == NULL || device == NULL) {
        snprintf(cb_error_text, sizeof(cb_error_text), "kein OD/CAN-Backend/Geraet angegeben");
        return -1;
    }

    if (backend->open(device, bitrate) != 0) {
        snprintf(cb_error_text, sizeof(cb_error_text), "%s '%s' oeffnen fehlgeschlagen: %s", backend->name, device,
                 strerror(errno));
        return -1;
    }
    cb_backend = backend;

    cb_co = CO_new((CO_config_t*)config, &heapUsed);
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

    err = CO_CANopenInit(cb_co, NULL, NULL, od, NULL, NMT_CONTROL, CB_CO_FIRST_HB_TIME_MS, CB_CO_SDO_SRV_TIMEOUT_MS,
                         CB_CO_SDO_TIMEOUT_MS, false, own_node_id, &errInfo);
    if (err != CO_ERROR_NO) {
        CO_delete(cb_co);
        cb_co = NULL;
        backend->close();
        if (err == CO_ERROR_OD_PARAMETERS) {
            return cb_fail("CO_CANopenInit: OD-Eintrag 0x%X fehlerhaft", (int)errInfo);
        }
        return cb_fail("CO_CANopenInit fehlgeschlagen: %d", (int)err);
    }

    err = CO_CANopenInitPDO(cb_co, cb_co->em, od, own_node_id, &errInfo);
    if (err != CO_ERROR_NO) {
        CO_delete(cb_co);
        cb_co = NULL;
        backend->close();
        return cb_fail("CO_CANopenInitPDO fehlgeschlagen: %d", (int)err);
    }

    CO_CANsetNormalMode(cb_co->CANmodule);

#if ((CO_CONFIG_GTW)&CO_CONFIG_GTW_ASCII) != 0
    /* Gateway-ASCII, wenn die App es im Config angefordert hat
     * (CNT_GTWA=1); CO_GTWA_init() hat CO_CANopenInit() erledigt. */
    if (cb_co->gtwa != NULL && config->CNT_GTWA == 1) {
        CO_GTWA_initRead(cb_co->gtwa, cb_gtwa_read_adapter, NULL);
        cb_gtwa_enabled = true;
    }
#endif

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
#if ((CO_CONFIG_GTW)&CO_CONFIG_GTW_ASCII) != 0
    cb_gtwa_enabled = false;
#endif
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

CO_t*
cb_co_handle(void) {
    return cb_co;
}

int
cb_co_nodstat(uint8_t filter_id, cb_co_nodstat_ent_t* out, size_t max) {
#if ((CO_CONFIG_HB_CONS)&CO_CONFIG_HB_CONS_ENABLE) == 0
    (void)filter_id;
    (void)out;
    (void)max;
    return -1;
#else
    const CO_HBconsumer_t* hb;
    int n = 0;

    if (!cb_co_connected() || cb_co == NULL || cb_co->HBcons == NULL) {
        return -1;
    }
    if (out == NULL && max > 0) {
        return -1;
    }

    hb = cb_co->HBcons;
    for (uint8_t i = 0; i < hb->numberOfMonitoredNodes; i++) {
        const CO_HBconsNode_t* node = &hb->monitoredNodes[i];

        /* Ueberwacht = mindestens ein Heartbeat empfangen und noch aktiv */
        if (node->HBstate != CO_HBconsumer_ACTIVE) {
            continue;
        }
        if (filter_id != 0 && node->nodeId != filter_id) {
            continue;
        }
        if ((size_t)n < max && out != NULL) {
            out[n].node_id = node->nodeId;
            out[n].nmt_state = (uint8_t)node->NMTstate;
        }
        n++;
    }
    return n;
#endif
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
        cb_sleep_us(1000);
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

/* Streamender SDO-Download: Clientpuffer per read_cb nachfuellen,
 * bufferPartial signalisiert dem Stack ausstehende Daten (Muster aus
 * 301/CO_SDOclient.h). Mit CO_CONFIG_SDO_CLI_BLOCK laeuft der Transfer
 * als Block-Download (Puffer dann 1000 Byte, sonst 32). */
static CO_SDO_abortCode_t
cb_write_SDO_stream(CO_SDOclient_t* SDO_C, uint8_t nodeId, uint16_t index, uint8_t subIndex, size_t totalSize,
                    uint16_t timeout_ms, cb_co_stream_read_t read_cb, void* read_ctx,
                    cb_co_stream_progress_t progress_cb, void* progress_ctx) {
    CO_SDO_return_t SDO_ret;
    uint8_t chunk[128]; /* von read_cb geliefert, noch nicht im Clientpuffer */
    size_t chunk_len = 0;
    size_t chunk_off = 0;
    bool source_eof = false;
    size_t fed = 0;             /* an den Clientpuffer uebergebene Bytes */
    size_t last_progress = 0;
    CO_SDO_abortCode_t abortCode = CO_SDO_AB_NONE;

    SDO_ret = CO_SDOclient_setup(SDO_C, CO_CAN_ID_SDO_CLI + nodeId, CO_CAN_ID_SDO_SRV + nodeId, nodeId);
    if (SDO_ret != CO_SDO_RT_ok_communicationEnd) {
        return CO_SDO_AB_GENERAL;
    }

    SDO_ret = CO_SDOclientDownloadInitiate(SDO_C, index, subIndex, totalSize, timeout_ms, true /* Block wenn moeglich */);
    if (SDO_ret != CO_SDO_RT_ok_communicationEnd) {
        return CO_SDO_AB_GENERAL;
    }

    do {
        /* Clientpuffer auffuellen, solange Quelle und Platz da sind */
        while (!source_eof || chunk_off < chunk_len) {
            if (chunk_off == chunk_len) {
                if (source_eof || fed >= totalSize) {
                    break;
                }
                int n = read_cb(read_ctx, chunk, sizeof(chunk));
                if (n < 0) {
                    /* Quelle kaputt (z.B. Flash-Lesefehler): abbrechen */
                    (void)CO_SDOclientDownload(SDO_C, 0, true, false, &abortCode, NULL, NULL);
                    return CO_SDO_AB_NO_DATA;
                }
                if (n == 0) {
                    source_eof = true;
                    break;
                }
                chunk_len = (size_t)n;
                chunk_off = 0;
            }
            size_t w = CO_SDOclientDownloadBufWrite(SDO_C, chunk + chunk_off, chunk_len - chunk_off);
            chunk_off += w;
            fed += w;
            if (w == 0) {
                break; /* Clientpuffer voll */
            }
        }

        bool_t bufferPartial = (fed < totalSize) || (chunk_off < chunk_len);

        uint32_t timeDifference_us = 1000;
        size_t sizeTransferred = 0;
        abortCode = CO_SDO_AB_NONE;

        SDO_ret = CO_SDOclientDownload(SDO_C, timeDifference_us, false, bufferPartial, &abortCode, &sizeTransferred,
                                       NULL);
        if (SDO_ret < 0) {
            return abortCode;
        }

        if (progress_cb != NULL && (sizeTransferred - last_progress >= 4096u || SDO_ret == 0)) {
            last_progress = sizeTransferred;
            progress_cb(progress_ctx, sizeTransferred, totalSize);
        }

        cb_sleep_us(1000);
    } while (SDO_ret > 0);

    return CO_SDO_AB_NONE;
}

int
cb_co_sdo_write_stream(uint8_t node_id, uint16_t index, uint8_t sub, size_t total_size, uint16_t timeout_ms,
                       cb_co_stream_read_t read_cb, void* read_ctx, cb_co_stream_progress_t progress_cb,
                       void* progress_ctx, uint32_t* abort_code) {
    if (!cb_co_connected() || cb_co->SDOclient == NULL || read_cb == NULL) {
        *abort_code = 0;
        return -1;
    }
    if (timeout_ms == 0) {
        timeout_ms = CB_CO_SDO_TIMEOUT_MS;
    }

    cb_mutex_lock(CB_MUTEX_SDO);
    CO_SDO_abortCode_t abort = cb_write_SDO_stream(&cb_co->SDOclient[0], node_id, index, sub, total_size, timeout_ms,
                                                   read_cb, read_ctx, progress_cb, progress_ctx);
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

#if ((CO_CONFIG_GTW)&CO_CONFIG_GTW_ASCII) != 0
void
cb_co_gtwa_set_read(cb_co_gtwa_read_t cb, void* user) {
    cb_gtwa_read_user = user;
    cb_gtwa_read_cb = cb;
    if (cb_running && cb_co != NULL && cb_co->gtwa != NULL) {
        CO_GTWA_initRead(cb_co->gtwa, cb_gtwa_read_adapter, NULL);
    }
}

int
cb_co_gtwa_write(const char* buf, size_t len) {
    if (!cb_running || cb_co == NULL || cb_co->gtwa == NULL || !cb_gtwa_enabled) {
        return -1;
    }
    /* CO_fifo ist Single-Producer/Single-Consumer-sicher: hier der
     * einspeisende Thread, gelesen wird im Mainline-Thread. */
    return (int)CO_GTWA_write(cb_co->gtwa, buf, len);
}
#endif /* CO_CONFIG_GTW_ASCII */

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
