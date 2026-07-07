/**
 * canboss_poc.h
 *
 * PoC-Hallenlichtsteuerung (JSON-getriebene 5-Spalten-UI) mit CAN-Anbindung
 * ueber den CANopenNode-Treiber.
 *
 * Die Konfiguration (Felder, Button-Beschriftungen, CAN-IDs, minp-Feedback-
 * Map) wird von tools/poc2lvgl.py aus poc/{hall,can}_config.json generiert
 * (App/generated/canboss_poc_gen.[ch]) — dieselben JSON-Dateien wie im
 * OxivGL-PoC (embassy examples/touch-projects).
 *
 * Protokoll (kompatibel zu examples/touch-hall-common):
 *  - TX id CANBOSS_POC_TX_ID: 6-Byte-One-Hot-Bitmaske des gedrueckten
 *    Buttons, Wiederholung alle CANBOSS_POC_REPEAT_MS solange gehalten,
 *    Release = 6 Nullbytes, Idle-Keepalive alle CANBOSS_POC_REFRESH_MS.
 *  - RX id CANBOSS_POC_MINP_ID: Pegelbits (minp-Map) steuern das
 *    Button-Highlight.
 *
 * Die Frames laufen als Raw-11-Bit-Frames ueber denselben FDCAN wie der
 * CANopen-Stack (TX ueber die CO_LOCK_CAN_SEND-gesicherte HAL-TX-FIFO,
 * RX ueber den Unmatched-Frame-Hook des CO-Treibers).
 */

#ifndef CANBOSS_POC_H_
#define CANBOSS_POC_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Hallenscreen aufbauen und laden (aus dem Hauptmenue aufgerufen). */
void canboss_poc_screen_create(void);

#ifdef __cplusplus
}
#endif

#endif /* CANBOSS_POC_H_ */
