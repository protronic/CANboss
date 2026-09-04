/**
 * ndjson_fw.h
 *
 * Speicher-Backend fuer die per Webapp hochgeladenen Firmware-Images
 * (ndjson "fw"-Kommandos). Die Operationen entsprechen 1:1 dem
 * fwstore von canBLEberry (Flash-Partition); CANboss selbst bringt
 * ein RAM-Backend mit (ndjson_fw_ram.c) fuer Targets mit genug RAM
 * bzw. native_sim.
 *
 * Semantik: write_begin/write/write_end sind ein sequenzieller Upload
 * (ein Schreiber zur Zeit); write_end prueft die CRC32 (IEEE, wie
 * zlib) ueber die abgelegten Daten gegen crc_expected und macht den
 * Slot erst dann gueltig. read() liest wahlfrei aus einem gueltigen
 * Slot (fuer das SDO-Streaming zum Zielknoten).
 */

#ifndef CB_NDJSON_FW_H_
#define CB_NDJSON_FW_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NDJSON_FW_NAME_MAX 43 /* ohne NUL */

typedef struct ndjson_fw_ops {
    int (*slot_count)(void);
    size_t (*slot_capacity)(void);
    /* 0 = belegt (Ausgaben gefuellt), -ENOENT = leer, sonst Fehler */
    int (*slot_info)(int slot, size_t* size, uint32_t* crc32, char* name, size_t name_size);
    int (*erase)(int slot);
    int (*write_begin)(int slot);
    int (*write)(const void* data, size_t len);
    int (*write_end)(const char* name, uint32_t crc_expected, uint32_t* crc_actual);
    void (*write_abort)(void);
    int (*read)(int slot, size_t off, void* buf, size_t len);
} ndjson_fw_ops_t;

/* RAM-Backend (ndjson_fw_ram.c): Slots als statische Puffer.
 * Groesse ueber CB_FW_RAM_SLOTS/CB_FW_RAM_SLOT_SIZE (Compile-Definitionen,
 * Default 1 Slot x 64 KiB). */
extern const ndjson_fw_ops_t ndjson_fw_ram;

#ifdef __cplusplus
}
#endif

#endif /* CB_NDJSON_FW_H_ */
