/* Automatisch generiert von tools/eds2tui.py - NICHT von Hand editieren.
 * Knoten 48 "Klimasensor" aus demo_sensor.eds
 * 11 Objekte, 17 Datenpunkte. Neu erzeugen mit: make gen */

#include "canboss_net.h"

static const cb_dp_t cb_net_node_48_dps[] = {
    { .index = 0x1000, .sub = 0x00, .dtype = CB_DT_U32, .access = CB_ACC_RO, .name = "Device type", .default_value = "0x00000194" },
    { .index = 0x1001, .sub = 0x00, .dtype = CB_DT_U8, .access = CB_ACC_RO, .name = "Error register", .default_value = "0x00" },
    { .index = 0x1008, .sub = 0x00, .dtype = CB_DT_STR, .access = CB_ACC_RO, .name = "Manufacturer device name", .default_value = "Demo-Sensor" },
    { .index = 0x1009, .sub = 0x00, .dtype = CB_DT_STR, .access = CB_ACC_RO, .name = "Manufacturer hardware version", .default_value = "1.0" },
    { .index = 0x100A, .sub = 0x00, .dtype = CB_DT_STR, .access = CB_ACC_RO, .name = "Manufacturer software version", .default_value = "1.0" },
    { .index = 0x2300, .sub = 0x00, .dtype = CB_DT_U8, .access = CB_ACC_RO, .name = "Highest sub-index supported", .default_value = "0x02" },
    { .index = 0x2300, .sub = 0x01, .dtype = CB_DT_F32, .access = CB_ACC_RO, .name = "Temperatur", .default_value = "0.0" },
    { .index = 0x2300, .sub = 0x02, .dtype = CB_DT_F32, .access = CB_ACC_RO, .name = "Relative Feuchte", .default_value = "0.0" },
    { .index = 0x2301, .sub = 0x00, .dtype = CB_DT_U8, .access = CB_ACC_RO, .name = "Highest sub-index supported", .default_value = "0x02" },
    { .index = 0x2301, .sub = 0x01, .dtype = CB_DT_F32, .access = CB_ACC_RO, .name = "Luftdruck hPa", .default_value = "0.0" },
    { .index = 0x2301, .sub = 0x02, .dtype = CB_DT_U32, .access = CB_ACC_RO, .name = "Messzaehler", .default_value = "0" },
    { .index = 0x2302, .sub = 0x00, .dtype = CB_DT_U8, .access = CB_ACC_RO, .name = "Alarmstatus", .default_value = "0x00" },
    { .index = 0x2303, .sub = 0x00, .dtype = CB_DT_U16, .access = CB_ACC_RW, .name = "Messintervall ms", .default_value = "1000" },
    { .index = 0x2304, .sub = 0x00, .dtype = CB_DT_U8, .access = CB_ACC_RO, .name = "Highest sub-index supported", .default_value = "0x02" },
    { .index = 0x2304, .sub = 0x01, .dtype = CB_DT_F32, .access = CB_ACC_RW, .name = "Temperatur max", .default_value = "60.0" },
    { .index = 0x2304, .sub = 0x02, .dtype = CB_DT_F32, .access = CB_ACC_RW, .name = "Relative Feuchte max", .default_value = "80.0" },
    { .index = 0x2305, .sub = 0x00, .dtype = CB_DT_BOOL, .access = CB_ACC_RO, .name = "Alarm aktiv", .default_value = "0" },
};

static const cb_obj_t cb_net_node_48_objs[] = {
    { .index = 0x1000, .name = "Device type", .dp_first = 0, .dp_count = 1 },
    { .index = 0x1001, .name = "Error register", .dp_first = 1, .dp_count = 1 },
    { .index = 0x1008, .name = "Manufacturer device name", .dp_first = 2, .dp_count = 1 },
    { .index = 0x1009, .name = "Manufacturer hardware version", .dp_first = 3, .dp_count = 1 },
    { .index = 0x100A, .name = "Manufacturer software version", .dp_first = 4, .dp_count = 1 },
    { .index = 0x2300, .name = "Klima", .dp_first = 5, .dp_count = 3 },
    { .index = 0x2301, .name = "Umgebung", .dp_first = 8, .dp_count = 3 },
    { .index = 0x2302, .name = "Alarmstatus", .dp_first = 11, .dp_count = 1 },
    { .index = 0x2303, .name = "Messintervall ms", .dp_first = 12, .dp_count = 1 },
    { .index = 0x2304, .name = "Grenzwerte", .dp_first = 13, .dp_count = 3 },
    { .index = 0x2305, .name = "Alarm aktiv", .dp_first = 16, .dp_count = 1 },
};

const cb_node_desc_t cb_net_node_48 = {
    .node_id = 48,
    .name = "Klimasensor",
    .eds_file = "demo_sensor.eds",
    .product_name = "Demo-Sensor",
    .vendor_name = "Protronic",
    .objs = cb_net_node_48_objs,
    .obj_count = 11,
    .dps = cb_net_node_48_dps,
    .dp_count = 17,
};
