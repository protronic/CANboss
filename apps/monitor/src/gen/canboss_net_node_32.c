/* Automatisch generiert von tools/eds2tui.py - NICHT von Hand editieren.
 * Knoten 32 "Antrieb" aus demo_drive.eds
 * 14 Objekte, 16 Datenpunkte. Neu erzeugen mit: make gen */

#include "canboss_net.h"

static const cb_dp_t cb_net_node_32_dps[] = {
    { .index = 0x1000, .sub = 0x00, .dtype = CB_DT_U32, .access = CB_ACC_RO, .name = "Device type", .default_value = "0x00020192" },
    { .index = 0x1001, .sub = 0x00, .dtype = CB_DT_U8, .access = CB_ACC_RO, .name = "Error register", .default_value = "0x00" },
    { .index = 0x1008, .sub = 0x00, .dtype = CB_DT_STR, .access = CB_ACC_RO, .name = "Manufacturer device name", .default_value = "Demo-Antrieb" },
    { .index = 0x1009, .sub = 0x00, .dtype = CB_DT_STR, .access = CB_ACC_RO, .name = "Manufacturer hardware version", .default_value = "1.0" },
    { .index = 0x100A, .sub = 0x00, .dtype = CB_DT_STR, .access = CB_ACC_RO, .name = "Manufacturer software version", .default_value = "1.0" },
    { .index = 0x2200, .sub = 0x00, .dtype = CB_DT_I32, .access = CB_ACC_RO, .name = "Motorstrom mA", .default_value = "0" },
    { .index = 0x2201, .sub = 0x00, .dtype = CB_DT_BOOL, .access = CB_ACC_RW, .name = "Bremse aktiv", .default_value = "0" },
    { .index = 0x2202, .sub = 0x00, .dtype = CB_DT_F32, .access = CB_ACC_RO, .name = "Temperatur Endstufe", .default_value = "0.0" },
    { .index = 0x6040, .sub = 0x00, .dtype = CB_DT_U16, .access = CB_ACC_RW, .name = "Controlword", .default_value = "0x0000" },
    { .index = 0x6041, .sub = 0x00, .dtype = CB_DT_U16, .access = CB_ACC_RO, .name = "Statusword", .default_value = "0x0000" },
    { .index = 0x6042, .sub = 0x00, .dtype = CB_DT_I16, .access = CB_ACC_RW, .name = "vl target velocity", .default_value = "0" },
    { .index = 0x6043, .sub = 0x00, .dtype = CB_DT_I16, .access = CB_ACC_RO, .name = "vl velocity demand", .default_value = "0" },
    { .index = 0x6044, .sub = 0x00, .dtype = CB_DT_I16, .access = CB_ACC_RO, .name = "vl velocity actual value", .default_value = "0" },
    { .index = 0x6046, .sub = 0x00, .dtype = CB_DT_U8, .access = CB_ACC_RO, .name = "Highest sub-index supported", .default_value = "0x02" },
    { .index = 0x6046, .sub = 0x01, .dtype = CB_DT_U32, .access = CB_ACC_RW, .name = "vl velocity min amount", .default_value = "0" },
    { .index = 0x6046, .sub = 0x02, .dtype = CB_DT_U32, .access = CB_ACC_RW, .name = "vl velocity max amount", .default_value = "30000" },
};

static const cb_obj_t cb_net_node_32_objs[] = {
    { .index = 0x1000, .name = "Device type", .dp_first = 0, .dp_count = 1 },
    { .index = 0x1001, .name = "Error register", .dp_first = 1, .dp_count = 1 },
    { .index = 0x1008, .name = "Manufacturer device name", .dp_first = 2, .dp_count = 1 },
    { .index = 0x1009, .name = "Manufacturer hardware version", .dp_first = 3, .dp_count = 1 },
    { .index = 0x100A, .name = "Manufacturer software version", .dp_first = 4, .dp_count = 1 },
    { .index = 0x2200, .name = "Motorstrom mA", .dp_first = 5, .dp_count = 1 },
    { .index = 0x2201, .name = "Bremse aktiv", .dp_first = 6, .dp_count = 1 },
    { .index = 0x2202, .name = "Temperatur Endstufe", .dp_first = 7, .dp_count = 1 },
    { .index = 0x6040, .name = "Controlword", .dp_first = 8, .dp_count = 1 },
    { .index = 0x6041, .name = "Statusword", .dp_first = 9, .dp_count = 1 },
    { .index = 0x6042, .name = "vl target velocity", .dp_first = 10, .dp_count = 1 },
    { .index = 0x6043, .name = "vl velocity demand", .dp_first = 11, .dp_count = 1 },
    { .index = 0x6044, .name = "vl velocity actual value", .dp_first = 12, .dp_count = 1 },
    { .index = 0x6046, .name = "vl velocity min max amount", .dp_first = 13, .dp_count = 3 },
};

const cb_node_desc_t cb_net_node_32 = {
    .node_id = 32,
    .name = "Antrieb",
    .eds_file = "demo_drive.eds",
    .product_name = "Demo-Antrieb",
    .vendor_name = "Protronic",
    .objs = cb_net_node_32_objs,
    .obj_count = 14,
    .dps = cb_net_node_32_dps,
    .dp_count = 16,
};
