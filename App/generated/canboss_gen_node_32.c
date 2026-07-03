/* Automatisch generiert von tools/eds2lvgl.py - NICHT von Hand editieren.
 * Knoten 32 "Antrieb" aus demo_drive.eds
 * 12 Datenpunkte. Neu erzeugen mit: make gen */

#include "canboss_gen.h"
#include "canboss_ui.h"

static const canboss_dp_t canboss_node_32_dps[] = {
    { .index = 0x1000, .sub = 0x00, .dtype = CB_DT_U32, .access = CB_ACC_RO, .has_limits = 1, .name = "Device type", .min = 0, .max = 2147483647 },
    { .index = 0x1001, .sub = 0x00, .dtype = CB_DT_U8, .access = CB_ACC_RO, .has_limits = 1, .name = "Error register", .min = 0, .max = 255 },
    { .index = 0x1008, .sub = 0x00, .dtype = CB_DT_STR, .access = CB_ACC_RO, .has_limits = 0, .name = "Manufacturer device name", .min = 0, .max = 0 },
    { .index = 0x2200, .sub = 0x00, .dtype = CB_DT_I32, .access = CB_ACC_RO, .has_limits = 1, .name = "Motorstrom mA", .min = -2147483648, .max = 2147483647 },
    { .index = 0x2201, .sub = 0x00, .dtype = CB_DT_BOOL, .access = CB_ACC_RW, .has_limits = 0, .name = "Bremse aktiv", .min = 0, .max = 0 },
    { .index = 0x6040, .sub = 0x00, .dtype = CB_DT_U16, .access = CB_ACC_RW, .has_limits = 1, .name = "Controlword", .min = 0, .max = 65535 },
    { .index = 0x6041, .sub = 0x00, .dtype = CB_DT_U16, .access = CB_ACC_RO, .has_limits = 1, .name = "Statusword", .min = 0, .max = 65535 },
    { .index = 0x6042, .sub = 0x00, .dtype = CB_DT_I16, .access = CB_ACC_RW, .has_limits = 1, .name = "vl target velocity", .min = -30000, .max = 30000 },
    { .index = 0x6043, .sub = 0x00, .dtype = CB_DT_I16, .access = CB_ACC_RO, .has_limits = 1, .name = "vl velocity demand", .min = -32768, .max = 32767 },
    { .index = 0x6044, .sub = 0x00, .dtype = CB_DT_I16, .access = CB_ACC_RO, .has_limits = 1, .name = "vl velocity actual value", .min = -32768, .max = 32767 },
    { .index = 0x6046, .sub = 0x01, .dtype = CB_DT_U32, .access = CB_ACC_RW, .has_limits = 1, .name = "vl velocity min max amount: vl velocity min amount", .min = 0, .max = 30000 },
    { .index = 0x6046, .sub = 0x02, .dtype = CB_DT_U32, .access = CB_ACC_RW, .has_limits = 1, .name = "vl velocity min max amount: vl velocity max amount", .min = 0, .max = 30000 },
};

static void canboss_node_32_screen_create(void);

const canboss_node_desc_t canboss_node_32 = {
    .node_id = 32,
    .name = "Antrieb",
    .eds_file = "demo_drive.eds",
    .dps = canboss_node_32_dps,
    .dp_count = 12,
    .screen_create = canboss_node_32_screen_create,
};

/* LVGL-Screen des Knotens: eine Widget-Zeile je EDS-Datenpunkt */
static void
canboss_node_32_screen_create(void) {
    lv_obj_t* cont = canboss_ui_screen_begin(&canboss_node_32);
    canboss_ui_add_value_row(cont, &canboss_node_32, &canboss_node_32_dps[0]); /* 0x1000.00 Device type */
    canboss_ui_add_value_row(cont, &canboss_node_32, &canboss_node_32_dps[1]); /* 0x1001.00 Error register */
    canboss_ui_add_value_row(cont, &canboss_node_32, &canboss_node_32_dps[2]); /* 0x1008.00 Manufacturer device name */
    canboss_ui_add_value_row(cont, &canboss_node_32, &canboss_node_32_dps[3]); /* 0x2200.00 Motorstrom mA */
    canboss_ui_add_switch_row(cont, &canboss_node_32, &canboss_node_32_dps[4]); /* 0x2201.00 Bremse aktiv */
    canboss_ui_add_spinbox_row(cont, &canboss_node_32, &canboss_node_32_dps[5]); /* 0x6040.00 Controlword */
    canboss_ui_add_value_row(cont, &canboss_node_32, &canboss_node_32_dps[6]); /* 0x6041.00 Statusword */
    canboss_ui_add_spinbox_row(cont, &canboss_node_32, &canboss_node_32_dps[7]); /* 0x6042.00 vl target velocity */
    canboss_ui_add_value_row(cont, &canboss_node_32, &canboss_node_32_dps[8]); /* 0x6043.00 vl velocity demand */
    canboss_ui_add_value_row(cont, &canboss_node_32, &canboss_node_32_dps[9]); /* 0x6044.00 vl velocity actual value */
    canboss_ui_add_spinbox_row(cont, &canboss_node_32, &canboss_node_32_dps[10]); /* 0x6046.01 vl velocity min max amount: vl velocity min amount */
    canboss_ui_add_spinbox_row(cont, &canboss_node_32, &canboss_node_32_dps[11]); /* 0x6046.02 vl velocity min max amount: vl velocity max amount */
}
