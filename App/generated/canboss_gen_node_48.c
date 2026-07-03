/* Automatisch generiert von tools/eds2lvgl.py - NICHT von Hand editieren.
 * Knoten 48 "DS301 Geraet" aus DS301_profile.eds
 * 3 Datenpunkte. Neu erzeugen mit: make gen */

#include "canboss_gen.h"
#include "canboss_ui.h"

static const canboss_dp_t canboss_node_48_dps[] = {
    { .index = 0x1000, .sub = 0x00, .dtype = CB_DT_U32, .access = CB_ACC_RO, .has_limits = 1, .name = "Device type", .min = 0, .max = 2147483647 },
    { .index = 0x1001, .sub = 0x00, .dtype = CB_DT_U8, .access = CB_ACC_RO, .has_limits = 1, .name = "Error register", .min = 0, .max = 255 },
    { .index = 0x1017, .sub = 0x00, .dtype = CB_DT_U16, .access = CB_ACC_RW, .has_limits = 1, .name = "Producer heartbeat time", .min = 0, .max = 65535 },
};

static void canboss_node_48_screen_create(void);

const canboss_node_desc_t canboss_node_48 = {
    .node_id = 48,
    .name = "DS301 Geraet",
    .eds_file = "DS301_profile.eds",
    .dps = canboss_node_48_dps,
    .dp_count = 3,
    .screen_create = canboss_node_48_screen_create,
};

/* LVGL-Screen des Knotens: eine Widget-Zeile je EDS-Datenpunkt */
static void
canboss_node_48_screen_create(void) {
    lv_obj_t* cont = canboss_ui_screen_begin(&canboss_node_48);
    canboss_ui_add_value_row(cont, &canboss_node_48, &canboss_node_48_dps[0]); /* 0x1000.00 Device type */
    canboss_ui_add_value_row(cont, &canboss_node_48, &canboss_node_48_dps[1]); /* 0x1001.00 Error register */
    canboss_ui_add_spinbox_row(cont, &canboss_node_48, &canboss_node_48_dps[2]); /* 0x1017.00 Producer heartbeat time */
}
