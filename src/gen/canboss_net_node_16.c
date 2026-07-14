/* Automatisch generiert von tools/eds2tui.py - NICHT von Hand editieren.
 * Knoten 16 "IO-Modul" aus demo_io.eds
 * 13 Objekte, 25 Datenpunkte. Neu erzeugen mit: make gen */

#include "canboss_net.h"

static const cb_dp_t cb_net_node_16_dps[] = {
    { .index = 0x1000, .sub = 0x00, .dtype = CB_DT_U32, .access = CB_ACC_RO, .name = "Device type", .default_value = "0x00060191" },
    { .index = 0x1001, .sub = 0x00, .dtype = CB_DT_U8, .access = CB_ACC_RO, .name = "Error register", .default_value = "0x00" },
    { .index = 0x1008, .sub = 0x00, .dtype = CB_DT_STR, .access = CB_ACC_RO, .name = "Manufacturer device name", .default_value = "Demo-IO" },
    { .index = 0x1009, .sub = 0x00, .dtype = CB_DT_STR, .access = CB_ACC_RO, .name = "Manufacturer hardware version", .default_value = "1.0" },
    { .index = 0x100A, .sub = 0x00, .dtype = CB_DT_STR, .access = CB_ACC_RO, .name = "Manufacturer software version", .default_value = "1.0" },
    { .index = 0x2000, .sub = 0x00, .dtype = CB_DT_U8, .access = CB_ACC_RO, .name = "Highest sub-index supported", .default_value = "0x04" },
    { .index = 0x2000, .sub = 0x01, .dtype = CB_DT_BOOL, .access = CB_ACC_RW, .name = "Ausgang 1", .default_value = "0" },
    { .index = 0x2000, .sub = 0x02, .dtype = CB_DT_BOOL, .access = CB_ACC_RW, .name = "Ausgang 2", .default_value = "0" },
    { .index = 0x2000, .sub = 0x03, .dtype = CB_DT_BOOL, .access = CB_ACC_RW, .name = "Ausgang 3", .default_value = "0" },
    { .index = 0x2000, .sub = 0x04, .dtype = CB_DT_BOOL, .access = CB_ACC_RW, .name = "Ausgang 4", .default_value = "0" },
    { .index = 0x2001, .sub = 0x00, .dtype = CB_DT_U8, .access = CB_ACC_RO, .name = "Highest sub-index supported", .default_value = "0x04" },
    { .index = 0x2001, .sub = 0x01, .dtype = CB_DT_I16, .access = CB_ACC_RO, .name = "Eingang 1", .default_value = "0" },
    { .index = 0x2001, .sub = 0x02, .dtype = CB_DT_I16, .access = CB_ACC_RO, .name = "Eingang 2", .default_value = "0" },
    { .index = 0x2001, .sub = 0x03, .dtype = CB_DT_I16, .access = CB_ACC_RO, .name = "Eingang 3", .default_value = "0" },
    { .index = 0x2001, .sub = 0x04, .dtype = CB_DT_I16, .access = CB_ACC_RO, .name = "Eingang 4", .default_value = "0" },
    { .index = 0x2002, .sub = 0x00, .dtype = CB_DT_U8, .access = CB_ACC_RW, .name = "Digitale Ausgaenge Maske", .default_value = "0x00" },
    { .index = 0x2003, .sub = 0x00, .dtype = CB_DT_U8, .access = CB_ACC_RO, .name = "Highest sub-index supported", .default_value = "0x04" },
    { .index = 0x2003, .sub = 0x01, .dtype = CB_DT_BOOL, .access = CB_ACC_RO, .name = "Eingang D1", .default_value = "0" },
    { .index = 0x2003, .sub = 0x02, .dtype = CB_DT_BOOL, .access = CB_ACC_RO, .name = "Eingang D2", .default_value = "0" },
    { .index = 0x2003, .sub = 0x03, .dtype = CB_DT_BOOL, .access = CB_ACC_RO, .name = "Eingang D3", .default_value = "0" },
    { .index = 0x2003, .sub = 0x04, .dtype = CB_DT_BOOL, .access = CB_ACC_RO, .name = "Eingang D4", .default_value = "0" },
    { .index = 0x2100, .sub = 0x00, .dtype = CB_DT_F32, .access = CB_ACC_RO, .name = "Temperatur", .default_value = "0.0" },
    { .index = 0x2101, .sub = 0x00, .dtype = CB_DT_I16, .access = CB_ACC_RW, .name = "Sollwert", .default_value = "0" },
    { .index = 0x2102, .sub = 0x00, .dtype = CB_DT_STR, .access = CB_ACC_RW, .name = "Geraetename", .default_value = "Demo-IO" },
    { .index = 0x2103, .sub = 0x00, .dtype = CB_DT_U16, .access = CB_ACC_RW, .name = "Filterzeit ms", .default_value = "10" },
};

static const cb_obj_t cb_net_node_16_objs[] = {
    { .index = 0x1000, .name = "Device type", .dp_first = 0, .dp_count = 1 },
    { .index = 0x1001, .name = "Error register", .dp_first = 1, .dp_count = 1 },
    { .index = 0x1008, .name = "Manufacturer device name", .dp_first = 2, .dp_count = 1 },
    { .index = 0x1009, .name = "Manufacturer hardware version", .dp_first = 3, .dp_count = 1 },
    { .index = 0x100A, .name = "Manufacturer software version", .dp_first = 4, .dp_count = 1 },
    { .index = 0x2000, .name = "Digitale Ausgaenge", .dp_first = 5, .dp_count = 5 },
    { .index = 0x2001, .name = "Analoge Eingaenge", .dp_first = 10, .dp_count = 5 },
    { .index = 0x2002, .name = "Digitale Ausgaenge Maske", .dp_first = 15, .dp_count = 1 },
    { .index = 0x2003, .name = "Digitale Eingaenge", .dp_first = 16, .dp_count = 5 },
    { .index = 0x2100, .name = "Temperatur", .dp_first = 21, .dp_count = 1 },
    { .index = 0x2101, .name = "Sollwert", .dp_first = 22, .dp_count = 1 },
    { .index = 0x2102, .name = "Geraetename", .dp_first = 23, .dp_count = 1 },
    { .index = 0x2103, .name = "Filterzeit ms", .dp_first = 24, .dp_count = 1 },
};

const cb_node_desc_t cb_net_node_16 = {
    .node_id = 16,
    .name = "IO-Modul",
    .eds_file = "demo_io.eds",
    .product_name = "Demo-IO",
    .vendor_name = "Protronic",
    .objs = cb_net_node_16_objs,
    .obj_count = 13,
    .dps = cb_net_node_16_dps,
    .dp_count = 25,
};
