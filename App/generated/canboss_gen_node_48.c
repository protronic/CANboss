/* Automatisch generiert von tools/eds2lvgl.py - NICHT von Hand editieren.
 * Knoten 48 "Klimasensor" aus demo_sensor.eds
 * 14 Datenpunkte. Neu erzeugen mit: make gen */

#include "canboss_gen.h"
#include "canboss_ui.h"

static const canboss_dp_t canboss_node_48_dps[] = {
    { .index = 0x1000, .sub = 0x00, .dtype = CB_DT_U32, .access = CB_ACC_RO, .has_limits = 1, .name = "Device type", .min = 0, .max = 2147483647 },
    { .index = 0x1001, .sub = 0x00, .dtype = CB_DT_U8, .access = CB_ACC_RO, .has_limits = 1, .name = "Error register", .min = 0, .max = 255 },
    { .index = 0x1008, .sub = 0x00, .dtype = CB_DT_STR, .access = CB_ACC_RO, .has_limits = 0, .name = "Manufacturer device name", .min = 0, .max = 0 },
    { .index = 0x1009, .sub = 0x00, .dtype = CB_DT_STR, .access = CB_ACC_RO, .has_limits = 0, .name = "Manufacturer hardware version", .min = 0, .max = 0 },
    { .index = 0x100A, .sub = 0x00, .dtype = CB_DT_STR, .access = CB_ACC_RO, .has_limits = 0, .name = "Manufacturer software version", .min = 0, .max = 0 },
    { .index = 0x2300, .sub = 0x01, .dtype = CB_DT_F32, .access = CB_ACC_RO, .has_limits = 1, .name = "Klima: Temperatur", .min = -2000000, .max = 2000000 },
    { .index = 0x2300, .sub = 0x02, .dtype = CB_DT_F32, .access = CB_ACC_RO, .has_limits = 1, .name = "Klima: Relative Feuchte", .min = -2000000, .max = 2000000 },
    { .index = 0x2301, .sub = 0x01, .dtype = CB_DT_F32, .access = CB_ACC_RO, .has_limits = 1, .name = "Umgebung: Luftdruck hPa", .min = -2000000, .max = 2000000 },
    { .index = 0x2301, .sub = 0x02, .dtype = CB_DT_U32, .access = CB_ACC_RO, .has_limits = 1, .name = "Umgebung: Messzaehler", .min = 0, .max = 2147483647 },
    { .index = 0x2302, .sub = 0x00, .dtype = CB_DT_U8, .access = CB_ACC_RO, .has_limits = 1, .name = "Alarmstatus", .min = 0, .max = 255 },
    { .index = 0x2303, .sub = 0x00, .dtype = CB_DT_U16, .access = CB_ACC_RW, .has_limits = 1, .name = "Messintervall ms", .min = 100, .max = 60000 },
    { .index = 0x2304, .sub = 0x01, .dtype = CB_DT_F32, .access = CB_ACC_RW, .has_limits = 1, .name = "Grenzwerte: Temperatur max", .min = -2000000, .max = 2000000 },
    { .index = 0x2304, .sub = 0x02, .dtype = CB_DT_F32, .access = CB_ACC_RW, .has_limits = 1, .name = "Grenzwerte: Relative Feuchte max", .min = -2000000, .max = 2000000 },
    { .index = 0x2305, .sub = 0x00, .dtype = CB_DT_BOOL, .access = CB_ACC_RO, .has_limits = 0, .name = "Alarm aktiv", .min = 0, .max = 0 },
};

static void canboss_node_48_screen_create(void);

const canboss_node_desc_t canboss_node_48 = {
    .node_id = 48,
    .name = "Klimasensor",
    .eds_file = "demo_sensor.eds",
    .dps = canboss_node_48_dps,
    .dp_count = 14,
    .screen_create = canboss_node_48_screen_create,
};

/* LVGL-Screen des Knotens: eine Widget-Zeile je EDS-Datenpunkt */
static void
canboss_node_48_screen_create(void) {
    lv_obj_t* cont = canboss_ui_screen_begin(&canboss_node_48);
    canboss_ui_add_value_row(cont, &canboss_node_48, &canboss_node_48_dps[0]); /* 0x1000.00 Device type */
    canboss_ui_add_value_row(cont, &canboss_node_48, &canboss_node_48_dps[1]); /* 0x1001.00 Error register */
    canboss_ui_add_value_row(cont, &canboss_node_48, &canboss_node_48_dps[2]); /* 0x1008.00 Manufacturer device name */
    canboss_ui_add_value_row(cont, &canboss_node_48, &canboss_node_48_dps[3]); /* 0x1009.00 Manufacturer hardware version */
    canboss_ui_add_value_row(cont, &canboss_node_48, &canboss_node_48_dps[4]); /* 0x100A.00 Manufacturer software version */
    canboss_ui_add_value_row(cont, &canboss_node_48, &canboss_node_48_dps[5]); /* 0x2300.01 Klima: Temperatur */
    canboss_ui_add_value_row(cont, &canboss_node_48, &canboss_node_48_dps[6]); /* 0x2300.02 Klima: Relative Feuchte */
    canboss_ui_add_value_row(cont, &canboss_node_48, &canboss_node_48_dps[7]); /* 0x2301.01 Umgebung: Luftdruck hPa */
    canboss_ui_add_value_row(cont, &canboss_node_48, &canboss_node_48_dps[8]); /* 0x2301.02 Umgebung: Messzaehler */
    canboss_ui_add_value_row(cont, &canboss_node_48, &canboss_node_48_dps[9]); /* 0x2302.00 Alarmstatus */
    canboss_ui_add_spinbox_row(cont, &canboss_node_48, &canboss_node_48_dps[10]); /* 0x2303.00 Messintervall ms */
    canboss_ui_add_spinbox_row(cont, &canboss_node_48, &canboss_node_48_dps[11]); /* 0x2304.01 Grenzwerte: Temperatur max */
    canboss_ui_add_spinbox_row(cont, &canboss_node_48, &canboss_node_48_dps[12]); /* 0x2304.02 Grenzwerte: Relative Feuchte max */
    canboss_ui_add_value_row(cont, &canboss_node_48, &canboss_node_48_dps[13]); /* 0x2305.00 Alarm aktiv */
}
