/**
 * canboss_sdo.h
 *
 * Asynchrone SDO-Client-Schicht auf Basis von CANopenNode (CO_SDOclient).
 *
 * Die LVGL-Oberflaeche stellt Lese-/Schreibauftraege in eine Queue.
 * Ein Worker-Thread arbeitet die Auftraege sequenziell ueber den SDO-Client
 * (OD-Eintrag 0x1280) ab: Expedited- und Segmented-Transfers zu beliebigen
 * Knoten des Netzwerks. Ergebnisse werden per Callback-freiem Polling
 * (Statusfeld im Auftrag) an die UI zurueckgemeldet, damit alle
 * LVGL-Aufrufe im LVGL-Task bleiben.
 *
 * Implementierung: zephyr-app/src/canboss_sdo_zephyr.c (k_msgq/k_thread).
 */

#ifndef CANBOSS_SDO_H_
#define CANBOSS_SDO_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "canboss_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CB_SDO_IDLE = 0,   /* Auftrag frei / Ergebnis abgeholt */
    CB_SDO_PENDING,    /* eingereiht, Worker arbeitet */
    CB_SDO_DONE,       /* erfolgreich, Daten gueltig */
    CB_SDO_ERROR,      /* fehlgeschlagen (Abort/Timeout), abort_code gesetzt */
} canboss_sdo_state_t;

typedef struct canboss_sdo_req {
    /* -- vom Aufrufer zu fuellen -- */
    uint8_t node_id;
    uint16_t index;
    uint8_t sub;
    bool write;                        /* false = SDO-Upload (lesen) */
    uint8_t data[CANBOSS_DP_DATA_MAX]; /* bei write: Nutzdaten (little-endian) */
    size_t len;                        /* bei write: Datenlaenge */

    /* -- vom Worker gefuellt -- */
    volatile canboss_sdo_state_t state;
    uint32_t abort_code; /* SDO-Abortcode bei CB_SDO_ERROR (0 = Timeout) */
} canboss_sdo_req_t;

/* Worker-Task und Auftragsqueue anlegen (nach canopen_app_init aufrufen,
 * vor dem Start des Schedulers). */
void canboss_sdo_init(void);

/* Auftrag einreihen. req muss bis state != CB_SDO_PENDING gueltig bleiben.
 * Rueckgabe false, wenn die Queue voll ist (Auftrag nicht angenommen). */
bool canboss_sdo_submit(canboss_sdo_req_t* req);

#ifdef __cplusplus
}
#endif

#endif /* CANBOSS_SDO_H_ */
