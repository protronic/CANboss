/**
 * jsonapi_fw_ram.c - RAM-Backend fuer jsonapi-Firmware-Slots
 *
 * Fuer Targets mit reichlich RAM (STM32H573: 640 KiB) und native_sim.
 * Slots/Groesse per Compile-Definition:
 *   CB_FW_RAM_SLOTS      (Default 1)
 *   CB_FW_RAM_SLOT_SIZE  (Default 64 KiB)
 *
 * Inhalt ist fluechtig - nach Reset sind die Slots leer. Fuer
 * persistente Slots das Flash-Backend der jeweiligen App verwenden
 * (canBLEberry: src/fwstore.c).
 */

#include "jsonapi_fw.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#ifndef CB_FW_RAM_SLOTS
#define CB_FW_RAM_SLOTS 1
#endif
#ifndef CB_FW_RAM_SLOT_SIZE
#define CB_FW_RAM_SLOT_SIZE (64 * 1024)
#endif

static uint8_t slot_data[CB_FW_RAM_SLOTS][CB_FW_RAM_SLOT_SIZE];
static struct {
    bool valid;
    size_t size;
    uint32_t crc32;
    char name[JSONAPI_FW_NAME_MAX + 1];
} slot_meta[CB_FW_RAM_SLOTS];

static int wr_slot = -1;
static size_t wr_len;

/* CRC32 (IEEE/zlib), bitweise - reicht fuer den einmaligen Check */
static uint32_t
crc32_ieee_sw(uint32_t crc, const uint8_t* buf, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static int
ram_slot_count(void) {
    return CB_FW_RAM_SLOTS;
}

static size_t
ram_slot_capacity(void) {
    return CB_FW_RAM_SLOT_SIZE;
}

static bool
slot_ok(int slot) {
    return slot >= 0 && slot < CB_FW_RAM_SLOTS;
}

static int
ram_slot_info(int slot, size_t* size, uint32_t* crc32, char* name, size_t name_size) {
    if (!slot_ok(slot)) {
        return -EINVAL;
    }
    if (!slot_meta[slot].valid) {
        return -ENOENT;
    }
    *size = slot_meta[slot].size;
    *crc32 = slot_meta[slot].crc32;
    strncpy(name, slot_meta[slot].name, name_size - 1);
    name[name_size - 1] = '\0';
    return 0;
}

static int
ram_erase(int slot) {
    if (!slot_ok(slot)) {
        return -EINVAL;
    }
    if (wr_slot == slot) {
        wr_slot = -1;
    }
    slot_meta[slot].valid = false;
    return 0;
}

static int
ram_write_begin(int slot) {
    if (!slot_ok(slot)) {
        return -EINVAL;
    }
    slot_meta[slot].valid = false;
    wr_slot = slot;
    wr_len = 0;
    return 0;
}

static int
ram_write(const void* data, size_t len) {
    if (wr_slot < 0) {
        return -EBADF;
    }
    if (wr_len + len > CB_FW_RAM_SLOT_SIZE) {
        return -ENOSPC;
    }
    memcpy(&slot_data[wr_slot][wr_len], data, len);
    wr_len += len;
    return 0;
}

static int
ram_write_end(const char* name, uint32_t crc_expected, uint32_t* crc_actual) {
    if (wr_slot < 0) {
        return -EBADF;
    }

    uint32_t crc = crc32_ieee_sw(0, slot_data[wr_slot], wr_len);

    if (crc_actual != NULL) {
        *crc_actual = crc;
    }
    if (crc != crc_expected) {
        wr_slot = -1;
        return -EILSEQ;
    }

    slot_meta[wr_slot].size = wr_len;
    slot_meta[wr_slot].crc32 = crc;
    memset(slot_meta[wr_slot].name, 0, sizeof(slot_meta[wr_slot].name));
    strncpy(slot_meta[wr_slot].name, (name != NULL) ? name : "", JSONAPI_FW_NAME_MAX);
    slot_meta[wr_slot].valid = true;
    wr_slot = -1;
    return 0;
}

static void
ram_write_abort(void) {
    wr_slot = -1;
}

static int
ram_read(int slot, size_t off, void* buf, size_t len) {
    if (!slot_ok(slot) || !slot_meta[slot].valid) {
        return -EINVAL;
    }
    if (off > slot_meta[slot].size || len > slot_meta[slot].size - off) {
        return -EINVAL;
    }
    memcpy(buf, &slot_data[slot][off], len);
    return 0;
}

const jsonapi_fw_ops_t jsonapi_fw_ram = {
    .slot_count = ram_slot_count,
    .slot_capacity = ram_slot_capacity,
    .slot_info = ram_slot_info,
    .erase = ram_erase,
    .write_begin = ram_write_begin,
    .write = ram_write,
    .write_end = ram_write_end,
    .write_abort = ram_write_abort,
    .read = ram_read,
};
