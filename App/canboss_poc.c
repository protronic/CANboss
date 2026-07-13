/**
 * canboss_poc.c
 *
 * PoC-Hallenlichtsteuerung: LVGL-5-Spalten-Screen aus den PoC-JSON-Tabellen
 * (App/generated/canboss_poc_gen.[ch]) mit CAN-Bitmasken-Protokoll ueber den
 * CANopenNode-FDCAN-Treiber.
 *
 * Ablauf (kompatibel zum OxivGL-PoC / examples/touch-hall-common):
 *  - Button gedrueckt  -> One-Hot-Payload sofort senden, danach alle
 *    CANBOSS_POC_REPEAT_MS wiederholen, solange gehalten
 *  - Button losgelassen -> Release-Payload (6 x 0x00)
 *  - nichts gedrueckt   -> Release-Keepalive alle CANBOSS_POC_REFRESH_MS
 *  - RX minp-Frame      -> Pegelbits steuern das Button-Highlight
 *
 * Das CAN-Backend ist ausgelagert (canboss_poc_can_tx/_rx in canboss_poc.h):
 * auf dem Target laeuft es ueber den CANopenNode-FDCAN
 * (App/canboss_poc_can_stm32.c), im Host-Build ueber SocketCAN
 * (host/canboss_poc_can_host.c). canboss_poc_can_rx() darf aus ISR-/
 * Thread-Kontext kommen -> nur Puffer + Flag, Verarbeitung im LVGL-Timer.
 */

#include "canboss_poc.h"
#include "canboss_poc_gen.h"
#include "canboss_ui.h"

#include <string.h>
#include <stdint.h>

#define POC_TX_PAYLOAD_LEN 6u

/* Farbpalette wie im OxivGL-PoC (hall_view) */
#define POC_SCREEN_BG        0xE7DCC8
#define POC_SURFACE          0xFFFDF8
#define POC_CARD_BG          0xFFFDF7
#define POC_CARD_BG_GROUP    0xFFF7E7
#define POC_BUTTON_BG        0xFCF7EC
#define POC_BUTTON_BG_ACTIVE 0xEEE8DB
#define POC_BUTTON_PRESSED   0xE8DDC8
#define POC_BORDER           0xE4D8C3
#define POC_BORDER_ACTIVE    0xC5BAA8
#define POC_TEXT             0x151515
#define POC_ACCENT           0xA37418

/* ------------------------------------------------------------------ */
/* Zustand                                                              */
/* ------------------------------------------------------------------ */

static lv_obj_t* poc_buttons[CANBOSS_POC_BUTTON_COUNT];
static bool poc_highlight[CANBOSS_POC_BUTTON_COUNT];
static lv_timer_t* poc_timer;
static lv_obj_t* poc_screen;

static int16_t poc_pressed = -1;         /* gehaltener Button-Index, -1 = keiner */
static uint32_t poc_next_repeat_tick;
static uint32_t poc_next_refresh_tick;

/* minp-RX-Puffer, beschrieben im FDCAN-ISR-Kontext (CO_CANrawRxHook) */
static volatile uint8_t poc_rx_data[8];
static volatile uint8_t poc_rx_len;
static volatile bool poc_rx_valid;

/* One-Hot-Payload fuer Button-Index bauen (255/negativ = Release) */
static void
poc_build_payload(int16_t button_index, uint8_t payload[POC_TX_PAYLOAD_LEN]) {
    memset(payload, 0, POC_TX_PAYLOAD_LEN);
    if (button_index < 0 || button_index >= (int16_t)(POC_TX_PAYLOAD_LEN * 8u)) {
        return;
    }
#if CANBOSS_POC_TX_LITTLE
    payload[button_index / 8] = (uint8_t)(1u << (button_index % 8));
#else
    {
        uint16_t be_bit = (uint16_t)(POC_TX_PAYLOAD_LEN * 8u - 1u - (uint16_t)button_index);
        payload[be_bit / 8u] = (uint8_t)(1u << (7u - (be_bit % 8u)));
    }
#endif
}

static void
poc_send_button(int16_t button_index) {
    uint8_t payload[POC_TX_PAYLOAD_LEN];
    poc_build_payload(button_index, payload);
    if (CANBOSS_POC_CAN_ENABLED) {
        (void)canboss_poc_can_tx(payload);
    }
    poc_next_refresh_tick = lv_tick_get() + CANBOSS_POC_REFRESH_MS;
}

/* ------------------------------------------------------------------ */
/* CAN-RX vom Backend (ISR-/Thread-Kontext!)                            */
/* ------------------------------------------------------------------ */

void
canboss_poc_can_rx(uint16_t ident, const uint8_t* data, uint8_t dlc) {
    if (ident != CANBOSS_POC_MINP_ID) {
        return;
    }
    uint8_t len = (dlc > 8u) ? 8u : dlc;
    for (uint8_t i = 0; i < len; i++) {
        poc_rx_data[i] = data[i];
    }
    for (uint8_t i = len; i < 8u; i++) {
        poc_rx_data[i] = 0;
    }
    poc_rx_len = len;
    poc_rx_valid = true;
}

static bool
poc_minp_level(uint32_t button_index) {
    const canboss_poc_minp_t* e = &canboss_poc_minp[button_index];
    if (!poc_rx_valid || e->active == 0 || e->byte_index >= poc_rx_len) {
        return false;
    }
    return (poc_rx_data[e->byte_index] >> e->bit_index) & 1u;
}

/* ------------------------------------------------------------------ */
/* Highlight-Styling                                                    */
/* ------------------------------------------------------------------ */

static void
poc_apply_highlight(uint32_t index, bool active) {
    lv_obj_t* btn = poc_buttons[index];
    if (btn == NULL) {
        return;
    }
    lv_obj_set_style_bg_color(btn, lv_color_hex(active ? POC_BUTTON_BG_ACTIVE : POC_BUTTON_BG), 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(active ? POC_BORDER_ACTIVE : POC_BORDER), 0);
}

/* ------------------------------------------------------------------ */
/* Timer: Repeat, Idle-Keepalive, Highlight-Abgleich (LVGL-Task)        */
/* ------------------------------------------------------------------ */

static void
poc_timer_cb(lv_timer_t* t) {
    (void)t;
    uint32_t now = lv_tick_get();

    if (poc_pressed >= 0) {
        if ((int32_t)(now - poc_next_repeat_tick) >= 0) {
            poc_send_button(poc_pressed);
            poc_next_repeat_tick = now + CANBOSS_POC_REPEAT_MS;
        }
    } else if ((int32_t)(now - poc_next_refresh_tick) >= 0) {
        poc_send_button(-1); /* Release-Keepalive */
    }

    /* minp-Pegel -> Button-Highlight */
    for (uint32_t i = 0; i < CANBOSS_POC_BUTTON_COUNT; i++) {
        bool active = poc_minp_level(i);
        if (active != poc_highlight[i]) {
            poc_highlight[i] = active;
            poc_apply_highlight(i, active);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Widget-Events                                                        */
/* ------------------------------------------------------------------ */

static void
poc_button_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    int16_t index = (int16_t)(intptr_t)lv_event_get_user_data(e);

    if (code == LV_EVENT_PRESSED) {
        poc_pressed = index;
        poc_send_button(index);
        poc_next_repeat_tick = lv_tick_get() + CANBOSS_POC_REPEAT_MS;
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (poc_pressed == index) {
            poc_pressed = -1;
            poc_send_button(-1); /* Release */
        }
    }
}

static void
poc_back_clicked(lv_event_t* e) {
    (void)e;
    canboss_ui_init();
}

/* Screen wird zerstoert (Navigation zurueck): Timer stoppen, Release senden */
static void
poc_screen_delete_cb(lv_event_t* e) {
    (void)e;
    if (poc_pressed >= 0) {
        poc_pressed = -1;
        poc_send_button(-1);
    }
    if (poc_timer != NULL) {
        lv_timer_delete(poc_timer);
        poc_timer = NULL;
    }
    memset(poc_buttons, 0, sizeof(poc_buttons));
    poc_screen = NULL;
}

/* ------------------------------------------------------------------ */
/* Screen-Aufbau                                                        */
/* ------------------------------------------------------------------ */

static lv_obj_t*
poc_make_column(lv_obj_t* parent, const char* eyebrow, const char* title, bool group) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(group ? POC_CARD_BG_GROUP : POC_CARD_BG), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(POC_BORDER), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* eyebrow_lbl = lv_label_create(card);
    lv_label_set_text(eyebrow_lbl, eyebrow);
    lv_obj_set_style_text_color(eyebrow_lbl, lv_color_hex(POC_ACCENT), 0);
    lv_obj_set_style_text_letter_space(eyebrow_lbl, 2, 0);

    lv_obj_t* title_lbl = lv_label_create(card);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(POC_TEXT), 0);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_20, 0);

    return card;
}

static void
poc_make_button(lv_obj_t* card, uint32_t index) {
    lv_obj_t* btn = lv_button_create(card);
    lv_obj_set_size(btn, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(POC_BUTTON_BG), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(POC_BUTTON_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, lv_color_hex(POC_BORDER), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, canboss_poc_button_labels[index]);
    lv_obj_set_style_text_color(lbl, lv_color_hex(POC_TEXT), 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(btn, poc_button_event_cb, LV_EVENT_PRESSED, (void*)(intptr_t)index);
    lv_obj_add_event_cb(btn, poc_button_event_cb, LV_EVENT_RELEASED, (void*)(intptr_t)index);
    lv_obj_add_event_cb(btn, poc_button_event_cb, LV_EVENT_PRESS_LOST, (void*)(intptr_t)index);

    poc_buttons[index] = btn;
    poc_highlight[index] = false;
}

void
canboss_poc_screen_create(void) {
    /* SDO-Bindungen des vorherigen Screens loesen, bevor er geloescht wird
     * (der canboss_ui-Refresh-Timer darf keine toten Widgets anfassen) */
    canboss_ui_release_bindings();

    memset(poc_buttons, 0, sizeof(poc_buttons));
    memset(poc_highlight, 0, sizeof(poc_highlight));
    poc_pressed = -1;

    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_pad_row(scr, 0, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(POC_SCREEN_BG), 0);
    lv_obj_add_event_cb(scr, poc_screen_delete_cb, LV_EVENT_DELETE, NULL);

    /* Header: Zurueck + Titel + Hallenname */
    lv_obj_t* header = lv_obj_create(scr);
    lv_obj_set_size(header, LV_PCT(100), 56);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(POC_SURFACE), 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* back = lv_button_create(header);
    lv_obj_t* back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Netzwerk");
    lv_obj_add_event_cb(back, poc_back_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text_fmt(title, "%s - %s", canboss_poc_page_title, canboss_poc_hall_name);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_flex_grow(title, 1);
    lv_obj_set_style_pad_left(title, 12, 0);

    /* 5-Spalten-Bereich: ein Card-Container je Feld + Zentral-Gruppe */
    lv_obj_t* cols = lv_obj_create(scr);
    lv_obj_set_size(cols, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(cols, 1);
    lv_obj_set_flex_flow(cols, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(cols, 10, 0);
    lv_obj_set_style_pad_column(cols, 8, 0);
    lv_obj_set_style_bg_color(cols, lv_color_hex(POC_SCREEN_BG), 0);
    lv_obj_set_style_border_width(cols, 0, 0);
    lv_obj_set_style_radius(cols, 0, 0);
    lv_obj_remove_flag(cols, LV_OBJ_FLAG_SCROLLABLE);

    for (uint32_t f = 0; f < CANBOSS_POC_FIELD_COUNT; f++) {
        lv_obj_t* card = poc_make_column(cols, canboss_poc_fields[f].eyebrow, canboss_poc_fields[f].title, false);
        for (uint32_t j = 0; j < 3; j++) {
            poc_make_button(card, f * 3u + j);
        }
    }
    lv_obj_t* group = poc_make_column(cols, "ZENTRAL", "Alle Felder", true);
    for (uint32_t j = 0; j < 3; j++) {
        poc_make_button(group, CANBOSS_POC_GROUP_BASE + j);
    }

    if (poc_timer == NULL) {
        poc_timer = lv_timer_create(poc_timer_cb, CANBOSS_POC_REPEAT_MS, NULL);
    }
    poc_next_refresh_tick = lv_tick_get() + CANBOSS_POC_REFRESH_MS;

    poc_screen = scr;
    canboss_ui_load_screen(scr);
}
