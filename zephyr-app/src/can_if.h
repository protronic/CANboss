/**
 * can_if.h
 *
 * CAN-Backend-Abstraktion des CANboss-Monitors.
 *
 * Der CANopenNode-Treiber (port/CO_driver.c) spricht ausschliesslich
 * diese Schnittstelle, damit der Bus-Zugang austauschbar bleibt:
 *
 *   - "socketcan": Linux SocketCAN (can0, vcan0, ...) - implementiert
 *   - "serial":    serieller CAN-Adapter - vorbereitet (can_serial.c)
 *
 * Alle Funktionen arbeiten mit klassischen 11-Bit-Datenframes; mehr
 * braucht CANopen (CiA 301) nicht.
 */

#ifndef CB_CAN_IF_H_
#define CB_CAN_IF_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t id; /* 11-Bit-CAN-Identifier */
    uint8_t dlc; /* 0..8 */
    bool rtr;
    uint8_t data[8];
} cb_can_frame_t;

typedef struct {
    const char* name; /* Backend-Name fuer --backend */

    /* Verbindung oeffnen. device: Interface ("can0") bzw. Geraetepfad
     * ("/dev/ttyACM0"), bitrate: nur fuer Backends relevant, die die
     * Bitrate selbst setzen (SocketCAN uebernimmt sie vom Interface).
     * Rueckgabe 0 bei Erfolg, sonst -1 (errno gesetzt). */
    int (*open)(const char* device, uint32_t bitrate);

    void (*close)(void);

    /* Einen Frame senden. Rueckgabe 0 bei Erfolg, sonst -1. */
    int (*send)(const cb_can_frame_t* frame);

    /* Einen Frame empfangen. Rueckgabe 1 = Frame gelesen, 0 = Timeout,
     * -1 = Fehler. timeout_ms < 0 blockiert unbegrenzt. */
    int (*recv)(cb_can_frame_t* frame, int timeout_ms);
} cb_can_backend_t;

/* Backend anhand des Namens suchen (NULL wenn unbekannt). */
const cb_can_backend_t* cb_can_backend_find(const char* name);

/* Verfuegbare Backend-Namen, NULL-terminiert (fuer --help). */
const char* const* cb_can_backend_names(void);

#ifdef __cplusplus
}
#endif

#endif /* CB_CAN_IF_H_ */
