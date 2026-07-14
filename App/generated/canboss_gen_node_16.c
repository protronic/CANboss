/* Automatisch generiert von tools/eds2lvgl.py - NICHT von Hand editieren.
 * Knoten 16 "IO-Modul" aus demo_io.eds
 * 22 Datenpunkte. Neu erzeugen mit: make gen */

#include "canboss_gen.h"
#include "canboss_ui.h"

static const canboss_dp_t canboss_node_16_dps[] = {
    { .index = 0x1000, .sub = 0x00, .dtype = CB_DT_U32, .access = CB_ACC_RO, .has_limits = 1, .name = "Device type", .min = 0, .max = 2147483647 },
    { .index = 0x1001, .sub = 0x00, .dtype = CB_DT_U8, .access = CB_ACC_RO, .has_limits = 1, .name = "Error register", .min = 0, .max = 255 },
    { .index = 0x1008, .sub = 0x00, .dtype = CB_DT_STR, .access = CB_ACC_RO, .has_limits = 0, .name = "Manufacturer device name", .min = 0, .max = 0 },
    { .index = 0x1009, .sub = 0x00, .dtype = CB_DT_STR, .access = CB_ACC_RO, .has_limits = 0, .name = "Manufacturer hardware version", .min = 0, .max = 0 },
    { .index = 0x100A, .sub = 0x00, .dtype = CB_DT_STR, .access = CB_ACC_RO, .has_limits = 0, .name = "Manufacturer software version", .min = 0, .max = 0 },
    { .index = 0x2000, .sub = 0x01, .dtype = CB_DT_BOOL, .access = CB_ACC_RW, .has_limits = 0, .name = "Digitale Ausgaenge: Ausgang 1", .min = 0, .max = 0 },
    { .index = 0x2000, .sub = 0x02, .dtype = CB_DT_BOOL, .access = CB_ACC_RW, .has_limits = 0, .name = "Digitale Ausgaenge: Ausgang 2", .min = 0, .max = 0 },
    { .index = 0x2000, .sub = 0x03, .dtype = CB_DT_BOOL, .access = CB_ACC_RW, .has_limits = 0, .name = "Digitale Ausgaenge: Ausgang 3", .min = 0, .max = 0 },
    { .index = 0x2000, .sub = 0x04, .dtype = CB_DT_BOOL, .access = CB_ACC_RW, .has_limits = 0, .name = "Digitale Ausgaenge: Ausgang 4", .min = 0, .max = 0 },
    { .index = 0x2001, .sub = 0x01, .dtype = CB_DT_I16, .access = CB_ACC_RO, .has_limits = 1, .name = "Analoge Eingaenge: Eingang 1", .min = -1000, .max = 1000 },
    { .index = 0x2001, .sub = 0x02, .dtype = CB_DT_I16, .access = CB_ACC_RO, .has_limits = 1, .name = "Analoge Eingaenge: Eingang 2", .min = -1000, .max = 1000 },
    { .index = 0x2001, .sub = 0x03, .dtype = CB_DT_I16, .access = CB_ACC_RO, .has_limits = 1, .name = "Analoge Eingaenge: Eingang 3", .min = -1000, .max = 1000 },
    { .index = 0x2001, .sub = 0x04, .dtype = CB_DT_I16, .access = CB_ACC_RO, .has_limits = 1, .name = "Analoge Eingaenge: Eingang 4", .min = -1000, .max = 1000 },
    { .index = 0x2002, .sub = 0x00, .dtype = CB_DT_U8, .access = CB_ACC_RW, .has_limits = 1, .name = "Digitale Ausgaenge Maske", .min = 0, .max = 15 },
    { .index = 0x2003, .sub = 0x01, .dtype = CB_DT_BOOL, .access = CB_ACC_RO, .has_limits = 0, .name = "Digitale Eingaenge: Eingang D1", .min = 0, .max = 0 },
    { .index = 0x2003, .sub = 0x02, .dtype = CB_DT_BOOL, .access = CB_ACC_RO, .has_limits = 0, .name = "Digitale Eingaenge: Eingang D2", .min = 0, .max = 0 },
    { .index = 0x2003, .sub = 0x03, .dtype = CB_DT_BOOL, .access = CB_ACC_RO, .has_limits = 0, .name = "Digitale Eingaenge: Eingang D3", .min = 0, .max = 0 },
    { .index = 0x2003, .sub = 0x04, .dtype = CB_DT_BOOL, .access = CB_ACC_RO, .has_limits = 0, .name = "Digitale Eingaenge: Eingang D4", .min = 0, .max = 0 },
    { .index = 0x2100, .sub = 0x00, .dtype = CB_DT_F32, .access = CB_ACC_RO, .has_limits = 1, .name = "Temperatur", .min = -2000000, .max = 2000000 },
    { .index = 0x2101, .sub = 0x00, .dtype = CB_DT_I16, .access = CB_ACC_RW, .has_limits = 1, .name = "Sollwert", .min = -1000, .max = 1000 },
    { .index = 0x2102, .sub = 0x00, .dtype = CB_DT_STR, .access = CB_ACC_RW, .has_limits = 0, .name = "Geraetename", .min = 0, .max = 0 },
    { .index = 0x2103, .sub = 0x00, .dtype = CB_DT_U16, .access = CB_ACC_RW, .has_limits = 1, .name = "Filterzeit ms", .min = 0, .max = 10000 },
};

static void canboss_node_16_screen_create(void);

const canboss_node_desc_t canboss_node_16 = {
    .node_id = 16,
    .name = "IO-Modul",
    .eds_file = "demo_io.eds",
    .dps = canboss_node_16_dps,
    .dp_count = 22,
    .screen_create = canboss_node_16_screen_create,
};

/* LVGL-Screen des Knotens: eine Widget-Zeile je EDS-Datenpunkt */
static void
canboss_node_16_screen_create(void) {
    lv_obj_t* cont = canboss_ui_screen_begin(&canboss_node_16);
    canboss_ui_add_value_row(cont, &canboss_node_16, &canboss_node_16_dps[0]); /* 0x1000.00 Device type */
    canboss_ui_add_value_row(cont, &canboss_node_16, &canboss_node_16_dps[1]); /* 0x1001.00 Error register */
    canboss_ui_add_value_row(cont, &canboss_node_16, &canboss_node_16_dps[2]); /* 0x1008.00 Manufacturer device name */
    canboss_ui_add_value_row(cont, &canboss_node_16, &canboss_node_16_dps[3]); /* 0x1009.00 Manufacturer hardware version */
    canboss_ui_add_value_row(cont, &canboss_node_16, &canboss_node_16_dps[4]); /* 0x100A.00 Manufacturer software version */
    canboss_ui_add_switch_row(cont, &canboss_node_16, &canboss_node_16_dps[5]); /* 0x2000.01 Digitale Ausgaenge: Ausgang 1 */
    canboss_ui_add_switch_row(cont, &canboss_node_16, &canboss_node_16_dps[6]); /* 0x2000.02 Digitale Ausgaenge: Ausgang 2 */
    canboss_ui_add_switch_row(cont, &canboss_node_16, &canboss_node_16_dps[7]); /* 0x2000.03 Digitale Ausgaenge: Ausgang 3 */
    canboss_ui_add_switch_row(cont, &canboss_node_16, &canboss_node_16_dps[8]); /* 0x2000.04 Digitale Ausgaenge: Ausgang 4 */
    canboss_ui_add_bar_row(cont, &canboss_node_16, &canboss_node_16_dps[9]); /* 0x2001.01 Analoge Eingaenge: Eingang 1 */
    canboss_ui_add_bar_row(cont, &canboss_node_16, &canboss_node_16_dps[10]); /* 0x2001.02 Analoge Eingaenge: Eingang 2 */
    canboss_ui_add_bar_row(cont, &canboss_node_16, &canboss_node_16_dps[11]); /* 0x2001.03 Analoge Eingaenge: Eingang 3 */
    canboss_ui_add_bar_row(cont, &canboss_node_16, &canboss_node_16_dps[12]); /* 0x2001.04 Analoge Eingaenge: Eingang 4 */
    canboss_ui_add_slider_row(cont, &canboss_node_16, &canboss_node_16_dps[13]); /* 0x2002.00 Digitale Ausgaenge Maske */
    canboss_ui_add_value_row(cont, &canboss_node_16, &canboss_node_16_dps[14]); /* 0x2003.01 Digitale Eingaenge: Eingang D1 */
    canboss_ui_add_value_row(cont, &canboss_node_16, &canboss_node_16_dps[15]); /* 0x2003.02 Digitale Eingaenge: Eingang D2 */
    canboss_ui_add_value_row(cont, &canboss_node_16, &canboss_node_16_dps[16]); /* 0x2003.03 Digitale Eingaenge: Eingang D3 */
    canboss_ui_add_value_row(cont, &canboss_node_16, &canboss_node_16_dps[17]); /* 0x2003.04 Digitale Eingaenge: Eingang D4 */
    canboss_ui_add_value_row(cont, &canboss_node_16, &canboss_node_16_dps[18]); /* 0x2100.00 Temperatur */
    canboss_ui_add_slider_row(cont, &canboss_node_16, &canboss_node_16_dps[19]); /* 0x2101.00 Sollwert */
    canboss_ui_add_text_row(cont, &canboss_node_16, &canboss_node_16_dps[20]); /* 0x2102.00 Geraetename */
    canboss_ui_add_spinbox_row(cont, &canboss_node_16, &canboss_node_16_dps[21]); /* 0x2103.00 Filterzeit ms */
}
