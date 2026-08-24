/**
 * sdo_value.h
 *
 * Konvertierung zwischen SDO-Rohdaten (little-endian Bytes) und
 * Anzeigetext, je CANopen-Datentyp. Ersetzt die GTWA-Typkuerzel
 * (u8/u16/.../str) des Rust-Originals (proto.rs::data_type_to_gtwa).
 */

#ifndef CB_SDO_VALUE_H_
#define CB_SDO_VALUE_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "canboss.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Rohdaten (LE) als Text formatieren. Rueckgabe 0 bei Erfolg. */
int cb_value_format(cb_dtype_t dtype, const uint8_t* data, size_t len, char* out, size_t out_size);

/* Anzeigetext eines Datenpunkts in Dezimal oder Hex umsetzen.
 * Ganzzahlen und BOOL werden neu formatiert; STR/F32/OCTET und
 * Fehlertexte bleiben unveraendert (Rueckgabe -1). */
int cb_value_display(cb_dtype_t dtype, const char* text, bool hex, char* out, size_t out_size);

/* Eingabetext in Rohdaten (LE) wandeln. Rueckgabe 0 bei Erfolg,
 * -1 bei ungueltigem Text. *len erhaelt die Datenlaenge. */
int cb_value_parse(cb_dtype_t dtype, const char* text, uint8_t* data, size_t data_size, size_t* len);

#ifdef __cplusplus
}
#endif

#endif /* CB_SDO_VALUE_H_ */
