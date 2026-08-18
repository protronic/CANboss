/**
 * jsonapi_slcan.h
 *
 * SLCAN-Bruecke (Lawicel-ASCII, wie slcand/python-can/CANUSB) im
 * NDJSON-Strom des Gateways: Zeilen, die nicht mit '{' beginnen,
 * reicht jsonapi.c hierher weiter - Raw-CAN parallel zum laufenden
 * CANopen-Stack, ueber dasselbe Backend (cb_co_raw_send bzw. der
 * gemeinsame Raw-RX-Hook).
 *
 * Kommandos (Antwort '\r' = OK, BEL '\a' = Fehler; nach BEL folgt ein
 * '\r', damit zeilenbasierte Clients wie die Webapp ihn sehen):
 *
 *   O / L      Kanal oeffnen (L = nur mithoeren; der Bus laeuft ohnehin
 *              im Normalmodus weiter, CANopen sendet parallel)
 *   C          Kanal schliessen (RX-Weiterleitung stoppt)
 *   tiiildd..  11-Bit-Datenframe senden  -> "z\r"
 *   riiil      11-Bit-RTR-Frame senden   -> "z\r"
 *   Zx         Timestamp an/aus (4 Hex-Ziffern ms, Wrap bei 60000)
 *   V          Versionsstring "Vhhss\r"
 *   N          Seriennummer "Nxxxx\r"
 *   F          Statusflags "Fxx\r" (Bit 3 = RX-Puffer-Ueberlauf seit
 *              der letzten Abfrage)
 *   S/s/M/m/U/A/X/W/P/p/Q  angenommen und ignoriert ("\r"), damit
 *              slcand & Co. ihre Init-Sequenz durchlaufen; Bitrate und
 *              Filter gehoeren dem CANopen-Stack bzw. dem Devicetree
 *   T/R        Extended-Frames: Fehler - die CAN-Schicht (can_if) ist
 *              bewusst auf klassische 11-Bit-Frames beschraenkt
 *
 * Empfangene Frames gehen bei offenem Kanal als "tiiildd..[ts]\r"
 * bzw. "riiil[ts]\r" raus - gepuffert im RX-Thread, ausgegeben aus
 * jsonapi_poll() (jsonapi_slcan_pump), interleaved mit den
 * NDJSON-Zeilen. Ein Host, der nur SLCAN spricht, bekommt nur
 * SLCAN-Antworten zu sehen (NDJSON-Events entstehen nur auf
 * NDJSON-Requests) - der Port funktioniert damit auch direkt an
 * slcand/python-can.
 */

#ifndef CB_JSONAPI_SLCAN_H_
#define CB_JSONAPI_SLCAN_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Byte-Senke fuer Antworten/Frames registrieren (jsonapi_init). */
void jsonapi_slcan_init(void (*out)(const char* buf, size_t len));

/* Eine Kommandozeile (ohne CR/LF, NUL-terminiert) verarbeiten;
 * nur aus dem poll-Thread rufen. */
void jsonapi_slcan_line(const char* line, size_t len);

/* Empfangenen Frame einreihen (Raw-RX-Hook, beliebiger Kontext);
 * ohne offenen Kanal ein No-op. */
void jsonapi_slcan_rx(uint16_t ident, const uint8_t* data, uint8_t dlc, bool rtr);

/* Gepufferte RX-Frames ausgeben; zyklisch aus jsonapi_poll(). */
void jsonapi_slcan_pump(void);

#ifdef __cplusplus
}
#endif

#endif /* CB_JSONAPI_SLCAN_H_ */
