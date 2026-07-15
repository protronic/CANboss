/**
 * canboss_ui.c
 *
 * Laufzeit hinter den generierten Screens: Widget-Fabriken, Bindungs-Pool
 * (Datenpunkt <-> Widget), zyklischer SDO-Refresh und Schreib-Handling.
 *
 * Alle LVGL-Zugriffe laufen im LVGL-Task (lv_timer + Widget-Events);
 * die SDO-Transfers erledigt der Worker aus canboss_sdo.c.
 */

#include "canboss_ui.h"
#include "canboss_sdo.h"
#include "canboss_poc.h"

#include <stdio.h>
#include <string.h>

#define CB_MAX_BINDINGS      96u   /* max. Datenpunkte pro geoeffnetem Screen */
#define CB_REFRESH_PERIOD_MS 100u  /* Timer-Raster */
#define CB_READ_INTERVAL_MS  1000u /* Zykluszeit je Datenpunkt */
#define CB_READS_PER_TICK    4u    /* Neuanforderungen pro Timer-Tick */
#define CB_EDIT_HOLD_MS      2000u /* nach Nutzereingabe: Refresh aussetzen */

typedef struct {
    bool active;                /* gehoert zum aktuell angezeigten Screen */
    const canboss_node_desc_t* node;
    const canboss_dp_t* dp;
    lv_obj_t* value_label;      /* ro-Zeile: Wertanzeige */
    lv_obj_t* widget;           /* rw-Zeile: switch/spinbox/textarea */
    canboss_sdo_req_t req;
    bool req_is_write;
    bool write_queued;          /* Schreibwunsch wartet, bis req frei ist */
    uint8_t wdata[CANBOSS_DP_DATA_MAX];
    size_t wlen;
    uint32_t next_read_tick;    /* fruehester Zeitpunkt des naechsten Reads */
    uint32_t rr_errors;         /* Fehlerzaehler in Folge */
} cb_binding_t;

static cb_binding_t cb_bindings[CB_MAX_BINDINGS];
static lv_timer_t* cb_timer;
static lv_obj_t* cb_cur_screen;
static lv_obj_t* cb_status_led;    /* Online-Anzeige im Screen-Header */
static lv_obj_t* cb_keyboard;      /* geteilte Bildschirmtastatur */
static const canboss_node_desc_t* cb_cur_node;

/* ------------------------------------------------------------------ */
/* Hilfen: Datentypen                                                   */
/* ------------------------------------------------------------------ */

static size_t
cb_dtype_size(uint8_t dtype) {
    switch ((canboss_dtype_t)dtype) {
        case CB_DT_BOOL:
        case CB_DT_I8:
        case CB_DT_U8: return 1;
        case CB_DT_I16:
        case CB_DT_U16: return 2;
        case CB_DT_I32:
        case CB_DT_U32:
        case CB_DT_F32: return 4;
        case CB_DT_I64:
        case CB_DT_U64: return 8;
        default: return 0; /* Strings: Laenge vom Transfer */
    }
}

static bool
cb_dtype_is_signed(uint8_t dtype) {
    switch ((canboss_dtype_t)dtype) {
        case CB_DT_I8:
        case CB_DT_I16:
        case CB_DT_I32:
        case CB_DT_I64: return true;
        default: return false;
    }
}

static int64_t
cb_decode_int(const uint8_t* data, size_t len, bool sign) {
    uint64_t u = 0;
    if (len > 8) {
        len = 8;
    }
    for (size_t i = 0; i < len; i++) {
        u |= (uint64_t)data[i] << (8u * i);
    }
    if (sign && len > 0 && len < 8 && (data[len - 1] & 0x80u)) {
        u |= ~((uint64_t)0) << (8u * len); /* Vorzeichen erweitern */
    }
    return (int64_t)u;
}

static void
cb_encode_int(uint8_t* data, size_t len, int64_t v) {
    for (size_t i = 0; i < len; i++) {
        data[i] = (uint8_t)(v >> (8u * i));
    }
}

static void
cb_format_value(const cb_binding_t* b, char* out, size_t outSize) {
    const uint8_t* d = (const uint8_t*)b->req.data;
    size_t len = b->req.len;

    switch ((canboss_dtype_t)b->dp->dtype) {
        case CB_DT_BOOL: snprintf(out, outSize, "%s", (len > 0 && d[0]) ? "EIN" : "AUS"); break;
        case CB_DT_F32: {
            float f = 0.0f;
            if (len >= 4) {
                memcpy(&f, d, 4);
            }
            snprintf(out, outSize, "%.3f", (double)f);
            break;
        }
        case CB_DT_STR: {
            size_t n = len < outSize - 1 ? len : outSize - 1;
            memcpy(out, d, n);
            out[n] = '\0';
            break;
        }
        case CB_DT_OCTET: {
            size_t pos = 0;
            out[0] = '\0';
            for (size_t i = 0; i < len && pos + 4 < outSize; i++) {
                pos += (size_t)snprintf(out + pos, outSize - pos, "%02X ", d[i]);
            }
            break;
        }
        default: {
            int64_t v = cb_decode_int(d, len, cb_dtype_is_signed(b->dp->dtype));
            /* newlib-nano: long long vermeiden, manuell formatieren */
            char tmp[24];
            int pos = 0;
            uint64_t u = (uint64_t)v;
            bool neg = false;
            if (cb_dtype_is_signed(b->dp->dtype) && v < 0) {
                neg = true;
                u = (uint64_t)(-v);
            }
            do {
                tmp[pos++] = (char)('0' + (u % 10u));
                u /= 10u;
            } while (u != 0 && pos < (int)sizeof(tmp));
            size_t o = 0;
            if (neg && o + 1 < outSize) {
                out[o++] = '-';
            }
            while (pos > 0 && o + 1 < outSize) {
                out[o++] = tmp[--pos];
            }
            out[o] = '\0';
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Bindungs-Pool                                                        */
/* ------------------------------------------------------------------ */

static cb_binding_t*
cb_binding_alloc(const canboss_node_desc_t* node, const canboss_dp_t* dp) {
    for (uint32_t i = 0; i < CB_MAX_BINDINGS; i++) {
        cb_binding_t* b = &cb_bindings[i];
        if (!b->active && b->req.state != CB_SDO_PENDING) {
            memset(b, 0, sizeof(*b));
            b->active = true;
            b->node = node;
            b->dp = dp;
            return b;
        }
    }
    return NULL;
}

/* Alle Bindungen des vorherigen Screens freigeben. Auftraege, die der
 * SDO-Worker noch haelt, bleiben als inaktive "Waisen" stehen, bis sie
 * abgeschlossen sind (der Pool-Slot wird erst danach wiederverwendet). */
static void
cb_bindings_release(void) {
    for (uint32_t i = 0; i < CB_MAX_BINDINGS; i++) {
        cb_bindings[i].active = false;
        cb_bindings[i].value_label = NULL;
        cb_bindings[i].widget = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Zyklischer Refresh + Ergebnisverarbeitung (laeuft im LVGL-Task)      */
/* ------------------------------------------------------------------ */

/* REAL32 in der Spinbox: Festkomma mit 3 Nachkommastellen (Wert x1000) */
#define CB_F32_SCALE 1000.0f

static int32_t
cb_f32_to_fixed(const cb_binding_t* b) {
    float f = 0.0f;
    if (b->req.len >= 4) {
        memcpy(&f, (const void*)b->req.data, 4);
    }
    f *= CB_F32_SCALE;
    return (int32_t)(f >= 0.0f ? f + 0.5f : f - 0.5f);
}

static void
cb_apply_read_result(cb_binding_t* b) {
    char text[64];

    if (b->widget == NULL) {
        if (b->value_label != NULL) {
            cb_format_value(b, text, sizeof(text));
            lv_label_set_text(b->value_label, text);
        }
        return;
    }
    /* Widget nicht ueberschreiben, solange der Nutzer editiert/drueckt */
    if (lv_obj_has_state(b->widget, LV_STATE_EDITED) || lv_obj_has_state(b->widget, LV_STATE_FOCUSED)
        || lv_obj_has_state(b->widget, LV_STATE_PRESSED)) {
        return;
    }

    if (lv_obj_check_type(b->widget, &lv_switch_class)) {
        bool on = b->req.len > 0 && b->req.data[0] != 0;
        if (on != lv_obj_has_state(b->widget, LV_STATE_CHECKED)) {
            if (on) {
                lv_obj_add_state(b->widget, LV_STATE_CHECKED);
            } else {
                lv_obj_remove_state(b->widget, LV_STATE_CHECKED);
            }
        }
    } else if (lv_obj_check_type(b->widget, &lv_spinbox_class)) {
        int32_t v;
        if (b->dp->dtype == CB_DT_F32) {
            v = cb_f32_to_fixed(b);
        } else {
            v = (int32_t)cb_decode_int(b->req.data, b->req.len, cb_dtype_is_signed(b->dp->dtype));
        }
        if (v != lv_spinbox_get_value(b->widget)) {
            lv_spinbox_set_value(b->widget, v);
        }
    } else if (lv_obj_check_type(b->widget, &lv_slider_class)) {
        int32_t v = (int32_t)cb_decode_int(b->req.data, b->req.len, cb_dtype_is_signed(b->dp->dtype));
        if (v != lv_slider_get_value(b->widget)) {
            lv_slider_set_value(b->widget, v, LV_ANIM_OFF);
        }
        if (b->value_label != NULL) {
            cb_format_value(b, text, sizeof(text));
            lv_label_set_text(b->value_label, text);
        }
    } else if (lv_obj_check_type(b->widget, &lv_bar_class)) {
        int32_t v = (int32_t)cb_decode_int(b->req.data, b->req.len, cb_dtype_is_signed(b->dp->dtype));
        lv_bar_set_value(b->widget, v, LV_ANIM_OFF);
        if (b->value_label != NULL) {
            cb_format_value(b, text, sizeof(text));
            lv_label_set_text(b->value_label, text);
        }
    } else if (lv_obj_check_type(b->widget, &lv_textarea_class)) {
        cb_format_value(b, text, sizeof(text));
        if (strcmp(text, lv_textarea_get_text(b->widget)) != 0) {
            lv_textarea_set_text(b->widget, text);
        }
    }
}

static void
cb_refresh_timer_cb(lv_timer_t* t) {
    (void)t;
    uint32_t now = lv_tick_get();
    uint32_t started = 0;
    bool any_ok = false;
    bool any_active = false;

    for (uint32_t i = 0; i < CB_MAX_BINDINGS; i++) {
        cb_binding_t* b = &cb_bindings[i];

        /* Ergebnisse einsammeln (auch von Waisen, um Slots freizugeben) */
        if (b->req.state == CB_SDO_DONE || b->req.state == CB_SDO_ERROR) {
            if (b->active) {
                if (b->req.state == CB_SDO_DONE) {
                    b->rr_errors = 0;
                    if (!b->req_is_write) {
                        cb_apply_read_result(b);
                    }
                } else {
                    b->rr_errors++;
                    if (!b->req_is_write && b->value_label != NULL && b->rr_errors >= 2) {
                        lv_label_set_text(b->value_label, "--");
                    }
                }
            }
            b->req.state = CB_SDO_IDLE;
        }

        if (!b->active) {
            continue;
        }
        any_active = true;
        if (b->rr_errors == 0 && b->next_read_tick != 0) {
            any_ok = true;
        }

        if (b->req.state != CB_SDO_IDLE) {
            continue;
        }

        /* Wartende Schreibauftraege zuerst */
        if (b->write_queued) {
            b->write_queued = false;
            b->req.node_id = b->node->node_id;
            b->req.index = b->dp->index;
            b->req.sub = b->dp->sub;
            b->req.write = true;
            memcpy((void*)b->req.data, b->wdata, b->wlen);
            b->req.len = b->wlen;
            b->req_is_write = true;
            canboss_sdo_submit(&b->req);
            continue;
        }

        /* Zyklisches Lesen (wo-Objekte lassen sich nicht lesen) */
        if (b->dp->access == CB_ACC_WO) {
            continue;
        }
        if (started < CB_READS_PER_TICK && (int32_t)(now - b->next_read_tick) >= 0) {
            b->req.node_id = b->node->node_id;
            b->req.index = b->dp->index;
            b->req.sub = b->dp->sub;
            b->req.write = false;
            b->req_is_write = false;
            if (canboss_sdo_submit(&b->req)) {
                started++;
            }
            b->next_read_tick = now + CB_READ_INTERVAL_MS;
        }
    }

    /* Online-Status im Header: gruen sobald mindestens ein Datenpunkt
     * des Knotens erfolgreich gelesen wurde */
    if (cb_status_led != NULL && any_active) {
        lv_obj_set_style_bg_color(cb_status_led, any_ok ? lv_palette_main(LV_PALETTE_GREEN)
                                                        : lv_palette_main(LV_PALETTE_RED), 0);
    }
}

/* ------------------------------------------------------------------ */
/* Schreiben aus Widget-Events                                          */
/* ------------------------------------------------------------------ */

static void
cb_queue_write(cb_binding_t* b, const uint8_t* data, size_t len) {
    if (len > CANBOSS_DP_DATA_MAX) {
        len = CANBOSS_DP_DATA_MAX;
    }
    memcpy(b->wdata, data, len);
    b->wlen = len;
    b->write_queued = true;
    b->next_read_tick = lv_tick_get() + CB_EDIT_HOLD_MS;
}

static void
cb_spinbox_inc_wrapper(lv_event_t* e) {
    lv_spinbox_increment((lv_obj_t*)lv_event_get_user_data(e));
    lv_obj_send_event((lv_obj_t*)lv_event_get_user_data(e), LV_EVENT_VALUE_CHANGED, NULL);
}

static void
cb_spinbox_dec_wrapper(lv_event_t* e) {
    lv_spinbox_decrement((lv_obj_t*)lv_event_get_user_data(e));
    lv_obj_send_event((lv_obj_t*)lv_event_get_user_data(e), LV_EVENT_VALUE_CHANGED, NULL);
}

static void
cb_back_clicked(lv_event_t* e) {
    (void)e;
    canboss_ui_init();
}

static void
cb_switch_event_cb(lv_event_t* e) {
    cb_binding_t* b = (cb_binding_t*)lv_event_get_user_data(e);
    if (!b->active) {
        return;
    }
    /* In voller Datentyp-Breite schreiben (Switch auch fuer Integer 0..1) */
    uint8_t data[8];
    size_t len = cb_dtype_size(b->dp->dtype);
    if (len == 0) {
        len = 1;
    }
    cb_encode_int(data, len, lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED) ? 1 : 0);
    cb_queue_write(b, data, len);
}

static void
cb_spinbox_event_cb(lv_event_t* e) {
    cb_binding_t* b = (cb_binding_t*)lv_event_get_user_data(e);
    if (!b->active) {
        return;
    }
    int32_t v = lv_spinbox_get_value((lv_obj_t*)lv_event_get_target(e));
    uint8_t data[8];
    size_t len;
    if (b->dp->dtype == CB_DT_F32) {
        float f = (float)v / CB_F32_SCALE;
        memcpy(data, &f, 4);
        len = 4;
    } else {
        len = cb_dtype_size(b->dp->dtype);
        cb_encode_int(data, len, v);
    }
    cb_queue_write(b, data, len);
}

/* Slider: Live-Label beim Ziehen, SDO-Download erst beim Loslassen */
static void
cb_slider_event_cb(lv_event_t* e) {
    cb_binding_t* b = (cb_binding_t*)lv_event_get_user_data(e);
    lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);

    if (b == NULL || !b->active) {
        return;
    }
    int32_t v = lv_slider_get_value(slider);

    if (code == LV_EVENT_VALUE_CHANGED) {
        if (b->value_label != NULL) {
            lv_label_set_text_fmt(b->value_label, "%d", (int)v);
        }
        /* Refresh waehrend des Ziehens zurueckhalten */
        b->next_read_tick = lv_tick_get() + CB_EDIT_HOLD_MS;
        return;
    }
    if (code == LV_EVENT_RELEASED) {
        uint8_t data[8];
        size_t len = cb_dtype_size(b->dp->dtype);
        cb_encode_int(data, len, v);
        cb_queue_write(b, data, len);
    }
}

static void
cb_textarea_event_cb(lv_event_t* e) {
    cb_binding_t* b = (cb_binding_t*)lv_event_get_user_data(e);
    lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_FOCUSED) {
        if (cb_keyboard != NULL) {
            lv_keyboard_set_textarea(cb_keyboard, ta);
            lv_obj_remove_flag(cb_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_CANCEL) {
        if (cb_keyboard != NULL) {
            lv_obj_add_flag(cb_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    if (code == LV_EVENT_READY) { /* Enter auf der Tastatur */
        if (b != NULL && b->active) {
            const char* txt = lv_textarea_get_text(ta);
            cb_queue_write(b, (const uint8_t*)txt, strlen(txt));
        }
        if (cb_keyboard != NULL) {
            lv_obj_add_flag(cb_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Screen-Aufbau                                                        */
/* ------------------------------------------------------------------ */

static lv_obj_t*
cb_row_create(lv_obj_t* cont, const canboss_dp_t* dp) {
    lv_obj_t* row = lv_obj_create(cont);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* left = lv_obj_create(row);
    lv_obj_set_size(left, LV_PCT(55), LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_pad_all(left, 0, 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* name = lv_label_create(left);
    lv_label_set_text(name, dp->name);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, LV_PCT(100));

    lv_obj_t* addr = lv_label_create(left);
    lv_label_set_text_fmt(addr, "0x%04X.%02X", dp->index, dp->sub);
    lv_obj_set_style_text_color(addr, lv_palette_main(LV_PALETTE_GREY), 0);

    return row;
}

lv_obj_t*
canboss_ui_screen_begin(const canboss_node_desc_t* node) {
    cb_bindings_release();
    cb_cur_node = node;

    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_pad_row(scr, 0, 0);

    /* Header */
    lv_obj_t* header = lv_obj_create(scr);
    lv_obj_set_size(header, LV_PCT(100), 56);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* back = lv_button_create(header);
    lv_obj_t* back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Netzwerk");
    lv_obj_add_event_cb(back, cb_back_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text_fmt(title, "Node %u  %s", node->node_id, node->name);
    lv_obj_set_flex_grow(title, 1);
    lv_obj_set_style_pad_left(title, 12, 0);

    cb_status_led = lv_obj_create(header);
    lv_obj_set_size(cb_status_led, 18, 18);
    lv_obj_set_style_radius(cb_status_led, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(cb_status_led, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_remove_flag(cb_status_led, LV_OBJ_FLAG_SCROLLABLE);

    /* Scrollbare Datenpunktliste */
    lv_obj_t* cont = lv_obj_create(scr);
    lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(cont, 1);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, 4, 0);
    lv_obj_set_style_radius(cont, 0, 0);

    /* Geteilte Tastatur fuer Text-Eingaben (zunaechst versteckt) */
    cb_keyboard = lv_keyboard_create(scr);
    lv_obj_add_flag(cb_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cb_keyboard, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(cb_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);

    canboss_ui_load_screen(scr);
    return cont;
}

void
canboss_ui_load_screen(lv_obj_t* scr) {
    lv_obj_t* old = cb_cur_screen;
    cb_cur_screen = scr;
    lv_screen_load(scr);
    if (old != NULL) {
        lv_obj_delete(old);
    }
}

void
canboss_ui_release_bindings(void) {
    cb_bindings_release();
    cb_cur_node = NULL;
    cb_status_led = NULL;
    cb_keyboard = NULL;
}

/* ------------------------------------------------------------------ */
/* Row-Fabriken (werden vom generierten Code aufgerufen)                */
/* ------------------------------------------------------------------ */

void
canboss_ui_add_value_row(lv_obj_t* cont, const canboss_node_desc_t* node, const canboss_dp_t* dp) {
    lv_obj_t* row = cb_row_create(cont, dp);
    cb_binding_t* b = cb_binding_alloc(node, dp);

    lv_obj_t* val = lv_label_create(row);
    lv_label_set_text(val, "--");
    lv_obj_set_style_text_font(val, &lv_font_montserrat_20, 0);

    if (b != NULL) {
        b->value_label = val;
    }
}

void
canboss_ui_add_bar_row(lv_obj_t* cont, const canboss_node_desc_t* node, const canboss_dp_t* dp) {
    lv_obj_t* row = cb_row_create(cont, dp);
    cb_binding_t* b = cb_binding_alloc(node, dp);

    lv_obj_t* box = lv_obj_create(row);
    lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_pad_row(box, 4, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* val = lv_label_create(box);
    lv_label_set_text(val, "--");
    lv_obj_set_style_text_font(val, &lv_font_montserrat_20, 0);

    lv_obj_t* bar = lv_bar_create(box);
    lv_obj_set_size(bar, 160, 12);
    lv_bar_set_range(bar, dp->min, dp->max);

    if (b != NULL) {
        b->value_label = val;
        b->widget = bar;
    }
}

void
canboss_ui_add_switch_row(lv_obj_t* cont, const canboss_node_desc_t* node, const canboss_dp_t* dp) {
    lv_obj_t* row = cb_row_create(cont, dp);
    cb_binding_t* b = cb_binding_alloc(node, dp);

    lv_obj_t* sw = lv_switch_create(row);
    if (b != NULL) {
        b->widget = sw;
        lv_obj_add_event_cb(sw, cb_switch_event_cb, LV_EVENT_VALUE_CHANGED, b);
    }
}

void
canboss_ui_add_slider_row(lv_obj_t* cont, const canboss_node_desc_t* node, const canboss_dp_t* dp) {
    lv_obj_t* row = cb_row_create(cont, dp);
    cb_binding_t* b = cb_binding_alloc(node, dp);

    lv_obj_t* box = lv_obj_create(row);
    lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_pad_row(box, 6, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* val = lv_label_create(box);
    lv_label_set_text(val, "--");
    lv_obj_set_style_text_font(val, &lv_font_montserrat_20, 0);

    lv_obj_t* slider = lv_slider_create(box);
    /* Breit genug fuer Fingerbedienung, mit Luft fuer den Knopf */
    lv_obj_set_width(slider, 200);
    lv_obj_set_style_margin_hor(slider, 10, 0);
    lv_slider_set_range(slider, dp->min, dp->max);

    if (b != NULL) {
        b->value_label = val;
        b->widget = slider;
        lv_obj_add_event_cb(slider, cb_slider_event_cb, LV_EVENT_VALUE_CHANGED, b);
        lv_obj_add_event_cb(slider, cb_slider_event_cb, LV_EVENT_RELEASED, b);
    }
}

void
canboss_ui_add_spinbox_row(lv_obj_t* cont, const canboss_node_desc_t* node, const canboss_dp_t* dp) {
    lv_obj_t* row = cb_row_create(cont, dp);
    cb_binding_t* b = cb_binding_alloc(node, dp);

    lv_obj_t* box = lv_obj_create(row);
    lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* dec = lv_button_create(box);
    lv_obj_set_style_bg_image_src(dec, LV_SYMBOL_MINUS, 0);
    lv_obj_set_size(dec, 40, 40);

    lv_obj_t* sb = lv_spinbox_create(box);
    lv_obj_set_width(sb, dp->dtype == CB_DT_F32 ? 140 : 110);
    if (dp->has_limits) {
        lv_spinbox_set_range(sb, dp->min, dp->max);
    } else {
        lv_spinbox_set_range(sb, INT32_MIN, INT32_MAX);
    }
    if (dp->dtype == CB_DT_F32) {
        /* Festkomma x1000: 5 Vorkomma- + 3 Nachkommastellen */
        lv_spinbox_set_digit_format(sb, 8, 5);
    } else {
        lv_spinbox_set_digit_format(sb, 8, 0);
    }

    lv_obj_t* inc = lv_button_create(box);
    lv_obj_set_style_bg_image_src(inc, LV_SYMBOL_PLUS, 0);
    lv_obj_set_size(inc, 40, 40);

    if (b != NULL) {
        b->widget = sb;
        lv_obj_add_event_cb(sb, cb_spinbox_event_cb, LV_EVENT_VALUE_CHANGED, b);
    }

    /* +/- Buttons mit der Spinbox verdrahten */
    lv_obj_add_event_cb(dec, cb_spinbox_dec_wrapper, LV_EVENT_CLICKED, sb);
    lv_obj_add_event_cb(inc, cb_spinbox_inc_wrapper, LV_EVENT_CLICKED, sb);
}

void
canboss_ui_add_text_row(lv_obj_t* cont, const canboss_node_desc_t* node, const canboss_dp_t* dp) {
    lv_obj_t* row = cb_row_create(cont, dp);
    cb_binding_t* b = cb_binding_alloc(node, dp);

    lv_obj_t* ta = lv_textarea_create(row);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, CANBOSS_DP_DATA_MAX - 1);
    lv_obj_set_width(ta, LV_PCT(40));

    if (b != NULL) {
        b->widget = ta;
        lv_obj_add_event_cb(ta, cb_textarea_event_cb, LV_EVENT_ALL, b);
    }
}

/* ------------------------------------------------------------------ */
/* Hauptmenue                                                           */
/* ------------------------------------------------------------------ */

static void
cb_menu_node_clicked(lv_event_t* e) {
    const canboss_node_desc_t* node = (const canboss_node_desc_t*)lv_event_get_user_data(e);
    if (node != NULL && node->screen_create != NULL) {
        node->screen_create();
    }
}

static void
cb_menu_poc_clicked(lv_event_t* e) {
    (void)e;
    canboss_poc_screen_create();
}

void
canboss_ui_init(void) {
    cb_bindings_release();
    cb_cur_node = NULL;
    cb_status_led = NULL;
    cb_keyboard = NULL;

    if (cb_timer == NULL) {
        cb_timer = lv_timer_create(cb_refresh_timer_cb, CB_REFRESH_PERIOD_MS, NULL);
    }

    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_pad_row(scr, 0, 0);

    lv_obj_t* header = lv_obj_create(scr);
    lv_obj_set_size(header, LV_PCT(100), 56);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "CANbossTouch - CANopen Netzwerk");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_center(title);

    lv_obj_t* list = lv_list_create(scr);
    lv_obj_set_size(list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_radius(list, 0, 0);

    for (uint16_t i = 0; i < canboss_node_count; i++) {
        const canboss_node_desc_t* node = canboss_nodes[i];
        char buf[80];
        snprintf(buf, sizeof(buf), "Node %u  %s  (%u Datenpunkte)", node->node_id, node->name, node->dp_count);
        lv_obj_t* btn = lv_list_add_button(list, LV_SYMBOL_USB, buf);
        lv_obj_add_event_cb(btn, cb_menu_node_clicked, LV_EVENT_CLICKED, (void*)node);
    }

    /* PoC-Hallenlichtsteuerung (JSON-getriebener Screen, Raw-CAN-Bitmasken) */
    lv_obj_t* poc_btn = lv_list_add_button(list, LV_SYMBOL_HOME, "Hallenlicht (PoC)");
    lv_obj_add_event_cb(poc_btn, cb_menu_poc_clicked, LV_EVENT_CLICKED, NULL);

    canboss_ui_load_screen(scr);
}
