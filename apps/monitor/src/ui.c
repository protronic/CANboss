/**
 * ui.c
 *
 * Terminal-UI, Port von CANboss-rs/src/ui.rs. Tastenbelegung wie im
 * Original (siehe README): Pfeile/Tab navigieren, '/' filtert,
 * 'r'/'R' liest per SDO, 'w' schreibt, 'm' schaltet den Auto-Refresh.
 */

#include "ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "canboss.h"
#include "canboss_net.h"
#include "co_node.h"
#include "osal.h"
#include "sdo_value.h"
#include "tui.h"
#include "tui_io.h"

#ifdef CANBOSS_BERRY
#include "canboss_berry.h"
#endif

#define CB_FILTER_MAX 64
#define CB_STATUS_MAX 160
#define CB_MONITOR_INTERVAL_MS 2000

typedef enum {
    SCREEN_NODES = 0, /* Netzwerk-Browser (Setup-/EDS-Discovery im Original) */
    SCREEN_MONITOR,
} cb_screen_t;

typedef struct {
    cb_screen_t screen;
    const char* can_label;

    /* Netzwerk-Browser */
    uint16_t node_list[64]; /* gefilterte Indizes in cb_net_nodes */
    uint16_t node_list_len;
    uint16_t selected_node;
    char node_filter[CB_FILTER_MAX];
    bool node_filter_mode;

    /* Monitor */
    const cb_node_desc_t* node; /* geoeffneter Knoten */
    uint16_t* obj_list;         /* gefilterte Indizes in node->objs */
    uint16_t obj_list_len;
    uint16_t selected_obj;
    uint16_t selected_sub; /* Position innerhalb des Objekts */
    uint16_t obj_scroll;
    char filter[CB_FILTER_MAX];
    bool filter_mode;
    bool write_mode;
    char write_buf[CB_VALUE_MAX];
    bool auto_refresh;

    /* Laufzeitwerte je Datenpunkt des geoeffneten Knotens ("" = noch
     * nicht gelesen, Anzeige faellt auf den EDS-Default zurueck) */
    char (*values)[CB_VALUE_MAX];

    char status[CB_STATUS_MAX];
} cb_ui_t;

static uint64_t
now_ms(void) {
    return cb_now_us() / 1000ull;
}

static void
set_status(cb_ui_t* ui, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ui->status, sizeof(ui->status), fmt, ap);
    va_end(ap);
}

static bool
str_contains_nocase(const char* hay, const char* needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0) {
        return true;
    }
    for (; *hay != '\0'; hay++) {
        if (strncasecmp(hay, needle, nlen) == 0) {
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Filter (Pendants zu apply_*_filter in ui.rs)                        */

static void
apply_node_filter(cb_ui_t* ui) {
    ui->node_list_len = 0;
    for (uint16_t i = 0; i < cb_net_node_count && ui->node_list_len < 64; i++) {
        const cb_node_desc_t* n = cb_net_nodes[i];
        char hay[192];
        snprintf(hay, sizeof(hay), "%u %s %s %s", n->node_id, n->name, n->eds_file, n->product_name);
        if (str_contains_nocase(hay, ui->node_filter)) {
            ui->node_list[ui->node_list_len++] = i;
        }
    }
    if (ui->selected_node >= ui->node_list_len) {
        ui->selected_node = ui->node_list_len > 0 ? (uint16_t)(ui->node_list_len - 1) : 0;
    }
}

static void
apply_obj_filter(cb_ui_t* ui) {
    ui->obj_list_len = 0;
    if (ui->node == NULL) {
        return;
    }
    for (uint16_t i = 0; i < ui->node->obj_count; i++) {
        const cb_obj_t* obj = &ui->node->objs[i];
        char hay[160];
        snprintf(hay, sizeof(hay), "0x%04X %s", obj->index, obj->name);
        if (str_contains_nocase(hay, ui->filter)) {
            ui->obj_list[ui->obj_list_len++] = i;
        }
    }
    if (ui->selected_obj >= ui->obj_list_len) {
        ui->selected_obj = ui->obj_list_len > 0 ? (uint16_t)(ui->obj_list_len - 1) : 0;
    }
    ui->selected_sub = 0;
}

static const cb_obj_t*
current_obj(const cb_ui_t* ui) {
    if (ui->node == NULL || ui->selected_obj >= ui->obj_list_len) {
        return NULL;
    }
    return &ui->node->objs[ui->obj_list[ui->selected_obj]];
}

static const cb_dp_t*
current_dp(const cb_ui_t* ui) {
    const cb_obj_t* obj = current_obj(ui);
    if (obj == NULL || ui->selected_sub >= obj->dp_count) {
        return NULL;
    }
    return &ui->node->dps[obj->dp_first + ui->selected_sub];
}

/* ------------------------------------------------------------------ */
/* Monitor oeffnen/schliessen                                          */

static int
open_node(cb_ui_t* ui, uint16_t registry_idx) {
    const cb_node_desc_t* node = cb_net_nodes[registry_idx];

    free(ui->values);
    free(ui->obj_list);
    ui->values = calloc(node->dp_count, CB_VALUE_MAX);
    ui->obj_list = calloc(node->obj_count, sizeof(uint16_t));
    if (ui->values == NULL || ui->obj_list == NULL) {
        free(ui->values);
        free(ui->obj_list);
        ui->values = NULL;
        ui->obj_list = NULL;
        set_status(ui, "Speicherfehler beim Oeffnen von %s", node->name);
        return -1;
    }

    ui->node = node;
    ui->filter[0] = '\0';
    ui->filter_mode = false;
    ui->write_mode = false;
    ui->selected_obj = 0;
    ui->selected_sub = 0;
    ui->obj_scroll = 0;
    ui->auto_refresh = false;
    apply_obj_filter(ui);
    ui->screen = SCREEN_MONITOR;
    set_status(ui, "Geladen: %s (Node %u, %u Objekte, %u Datenpunkte)", node->name, node->node_id, node->obj_count,
               node->dp_count);
    return 0;
}

static void
close_node(cb_ui_t* ui) {
    free(ui->values);
    free(ui->obj_list);
    ui->values = NULL;
    ui->obj_list = NULL;
    ui->node = NULL;
    ui->auto_refresh = false;
    ui->screen = SCREEN_NODES;
    set_status(ui, "Netzwerk-Browser");
}

/* ------------------------------------------------------------------ */
/* SDO-Aktionen (Pendants zu read_selected/read_object/write_selected) */

static void
read_dp(cb_ui_t* ui, const cb_obj_t* obj, uint16_t sub_pos) {
    const cb_dp_t* dp = &ui->node->dps[obj->dp_first + sub_pos];
    uint8_t buf[CB_DP_DATA_MAX];
    size_t got = 0;
    uint32_t abort = 0;
    char* value = ui->values[obj->dp_first + sub_pos];

    if (cb_co_sdo_read(ui->node->node_id, dp->index, dp->sub, buf, sizeof(buf), &got, &abort) != 0) {
        snprintf(value, CB_VALUE_MAX, "<%s>", cb_co_abort_str(abort));
        return;
    }
    if (cb_value_format((cb_dtype_t)dp->dtype, buf, got, value, CB_VALUE_MAX) != 0) {
        snprintf(value, CB_VALUE_MAX, "<Formatfehler>");
    }
}

static void
read_selected(cb_ui_t* ui) {
    const cb_obj_t* obj = current_obj(ui);
    const cb_dp_t* dp = current_dp(ui);
    if (obj == NULL || dp == NULL) {
        set_status(ui, "Kein Datenpunkt ausgewaehlt");
        return;
    }
    if (dp->access == CB_ACC_WO) {
        set_status(ui, "0x%04X.%02X ist nicht lesbar", dp->index, dp->sub);
        return;
    }
    if (!cb_co_connected()) {
        set_status(ui, "CAN nicht verbunden (Offline-Modus)");
        return;
    }
    read_dp(ui, obj, ui->selected_sub);
    set_status(ui, "Gelesen: 0x%04X.%02X %s", dp->index, dp->sub, dp->name);
}

static void
read_object(cb_ui_t* ui) {
    const cb_obj_t* obj = current_obj(ui);
    if (obj == NULL) {
        set_status(ui, "Kein Objekt ausgewaehlt");
        return;
    }
    if (!cb_co_connected()) {
        set_status(ui, "CAN nicht verbunden (Offline-Modus)");
        return;
    }
    uint16_t read_count = 0;
    for (uint16_t s = 0; s < obj->dp_count; s++) {
        if (ui->node->dps[obj->dp_first + s].access != CB_ACC_WO) {
            read_dp(ui, obj, s);
            read_count++;
        }
    }
    if (read_count == 0) {
        set_status(ui, "Keine lesbaren Datenpunkte");
    } else {
        set_status(ui, "%u Wert(e) von 0x%04X %s gelesen", read_count, obj->index, obj->name);
    }
}

static void
write_selected(cb_ui_t* ui) {
    const cb_obj_t* obj = current_obj(ui);
    const cb_dp_t* dp = current_dp(ui);
    if (obj == NULL || dp == NULL) {
        set_status(ui, "Kein Datenpunkt ausgewaehlt");
        return;
    }

    /* Eingabe trimmen */
    char text[CB_VALUE_MAX];
    snprintf(text, sizeof(text), "%s", ui->write_buf);
    char* p = text;
    while (*p == ' ') {
        p++;
    }
    size_t plen = strlen(p);
    while (plen > 0 && p[plen - 1] == ' ') {
        p[--plen] = '\0';
    }
    if (plen == 0 && dp->dtype != CB_DT_STR) {
        set_status(ui, "Wert eingeben");
        return;
    }

    if (!cb_co_connected()) {
        set_status(ui, "CAN nicht verbunden (Offline-Modus)");
        return;
    }

    uint8_t buf[CB_DP_DATA_MAX];
    size_t len = 0;
    if (cb_value_parse((cb_dtype_t)dp->dtype, p, buf, sizeof(buf), &len) != 0) {
        set_status(ui, "Ungueltiger Wert '%s' fuer %s", p, cb_dtype_name((cb_dtype_t)dp->dtype));
        return;
    }

    uint32_t abort = 0;
    if (cb_co_sdo_write(ui->node->node_id, dp->index, dp->sub, buf, len, &abort) != 0) {
        set_status(ui, "Schreiben fehlgeschlagen: %s", cb_co_abort_str(abort));
        return;
    }

    snprintf(ui->values[obj->dp_first + ui->selected_sub], CB_VALUE_MAX, "%s", p);
    ui->write_mode = false;
    ui->write_buf[0] = '\0';
    set_status(ui, "Geschrieben: 0x%04X.%02X %s = %s", dp->index, dp->sub, dp->name, p);
}

/* ------------------------------------------------------------------ */
/* Berry-REPL (Taste 's'): TUI kurz verlassen, REPL auf demselben      */
/* Raw-Mode-Terminal fahren, danach zeichnet die Hauptschleife neu     */

#ifdef CANBOSS_BERRY
static void
run_berry_repl(cb_ui_t* ui) {
    tui_io_write("\x1b[?25h", 6); /* Cursor einblenden */
    cb_berry_repl_io_t io = {tui_io_getc, tui_io_write};
    canboss_berry_repl(&io);
    tui_io_write("\x1b[?25l", 6); /* Cursor wieder aus, TUI uebernimmt */
    set_status(ui, "Berry-REPL beendet - help() zeigt dort Beispiele");
}
#endif

/* ------------------------------------------------------------------ */
/* Zeichnen                                                            */

static void
draw_status_bar(const cb_ui_t* ui, const char* help) {
    int rows = tui_rows();
    int cols = tui_cols();
    char line[512];

    const char* conn = cb_co_connected() ? "verbunden" : "offline";
    char context[160];
    if (ui->node != NULL) {
        snprintf(context, sizeof(context), "%s / Node %u", ui->node->name, ui->node->node_id);
    } else {
        snprintf(context, sizeof(context), "kein Geraet geladen");
    }

    char mode[224];
    if (ui->node_filter_mode) {
        snprintf(mode, sizeof(mode), "FILTER: %s", ui->node_filter);
    } else if (ui->filter_mode) {
        snprintf(mode, sizeof(mode), "FILTER: %s", ui->filter);
    } else if (ui->write_mode) {
        snprintf(mode, sizeof(mode), "WRITE: %s", ui->write_buf);
    } else {
        snprintf(mode, sizeof(mode), "%s", ui->status);
    }

    tui_box(rows - 4, 0, 4, cols, CB_ST_NONE, "Status");
    snprintf(line, sizeof(line), "%s | %s: %s | Monitor: %s | %s", context, ui->can_label, conn,
             ui->auto_refresh ? "an" : "aus", mode);
    tui_put(rows - 3, 2, CB_ST_NONE, line);
    tui_put(rows - 2, 2, CB_ST_DIM, help);
}

static void
draw_nodes(cb_ui_t* ui) {
    int rows = tui_rows();
    int cols = tui_cols();
    char line[512];

    snprintf(line, sizeof(line), "CAN-Netzwerk - eds/network.json (%u/%u angezeigt)", ui->node_list_len,
             cb_net_node_count);
    tui_box(0, 0, rows - 4, cols, CB_ST_NONE, line);

    snprintf(line, sizeof(line), "%-6s %-16s %-20s %-24s %-10s", "Node", "Name", "EDS", "Produkt", "Objekte");
    tui_put(1, 2, CB_ST_CYAN | CB_ST_BOLD, line);

    for (uint16_t row = 0; row < ui->node_list_len && (int)row + 2 < rows - 5; row++) {
        const cb_node_desc_t* n = cb_net_nodes[ui->node_list[row]];
        char detail[24];
        snprintf(detail, sizeof(detail), "%u/%u", n->obj_count, n->dp_count);
        snprintf(line, sizeof(line), "%-6u %-16.16s %-20.20s %-24.24s %-10s", n->node_id, n->name, n->eds_file,
                 n->product_name, detail);
        tui_put(2 + row, 2, row == ui->selected_node ? (CB_ST_REVERSE | CB_ST_BOLD) : CB_ST_NONE, line);
    }

#ifdef CANBOSS_BERRY
    draw_status_bar(ui, "q Ende | Pfeile Auswahl | Enter Monitor | / Filter | s Berry-REPL");
#else
    draw_status_bar(ui, "q Ende | Pfeile Auswahl | Enter Monitor | / Filter");
#endif
}

static void
draw_monitor(cb_ui_t* ui) {
    int rows = tui_rows();
    int cols = tui_cols();
    char line[512];
    int left_w = cols * 38 / 100;
    if (left_w < 24) {
        left_w = cols / 2;
    }
    int list_h = rows - 4;

    /* Linkes Fenster: Objektliste */
    snprintf(line, sizeof(line), "Objekte (%u) [%s]", ui->obj_list_len, ui->node->eds_file);
    tui_box(0, 0, list_h, left_w, CB_ST_NONE, line);

    int visible = list_h - 2;
    if (ui->selected_obj < ui->obj_scroll) {
        ui->obj_scroll = ui->selected_obj;
    }
    if (visible > 0 && ui->selected_obj >= ui->obj_scroll + visible) {
        ui->obj_scroll = (uint16_t)(ui->selected_obj - visible + 1);
    }

    for (int row = 0; row < visible && ui->obj_scroll + row < ui->obj_list_len; row++) {
        uint16_t idx = (uint16_t)(ui->obj_scroll + row);
        const cb_obj_t* obj = &ui->node->objs[ui->obj_list[idx]];
        snprintf(line, sizeof(line), "0x%04X %s", obj->index, obj->name);
        line[left_w - 3 > 0 ? left_w - 3 : 0] = '\0';
        tui_put(1 + row, 1, idx == ui->selected_obj ? (CB_ST_REVERSE | CB_ST_BOLD) : CB_ST_NONE, line);
    }

    /* Rechtes Fenster: Datenpunkte des gewaehlten Objekts */
    const cb_obj_t* obj = current_obj(ui);
    if (obj != NULL) {
        snprintf(line, sizeof(line), "0x%04X %s", obj->index, obj->name);
    } else {
        snprintf(line, sizeof(line), "Datenpunkte");
    }
    tui_box(0, left_w, list_h, cols - left_w, CB_ST_NONE, line);

    snprintf(line, sizeof(line), "%-4s %-28s %-10s %-4s %s", "Sub", "Name", "Typ", "Zug", "Wert");
    tui_put(1, left_w + 2, CB_ST_CYAN | CB_ST_BOLD, line);

    if (obj != NULL) {
        for (uint16_t s = 0; s < obj->dp_count && (int)s + 2 < list_h - 1; s++) {
            const cb_dp_t* dp = &ui->node->dps[obj->dp_first + s];
            const char* value = ui->values[obj->dp_first + s];
            char shown[CB_VALUE_MAX + 12];
            if (value[0] != '\0') {
                snprintf(shown, sizeof(shown), "%s", value);
            } else if (dp->default_value[0] != '\0') {
                snprintf(shown, sizeof(shown), "%s (default)", dp->default_value);
            } else {
                snprintf(shown, sizeof(shown), "-");
            }
            snprintf(line, sizeof(line), "%02X   %-28.28s %-10s %-4s %s", dp->sub, dp->name,
                     cb_dtype_name((cb_dtype_t)dp->dtype), cb_access_name((cb_access_t)dp->access), shown);
            tui_put(2 + s, left_w + 2, s == ui->selected_sub ? (CB_ST_YELLOW | CB_ST_BOLD) : CB_ST_NONE, line);
        }
    }

#ifdef CANBOSS_BERRY
    draw_status_bar(
        ui, "q Ende | b zurueck | Pfeile Objekt/Sub | / Filter | r lesen | R alle | w schreiben | m Monitor | s REPL");
#else
    draw_status_bar(ui,
                    "q Ende | b zurueck | Pfeile Objekt/Sub | / Filter | r lesen | R alle | w schreiben | m Monitor");
#endif
}

/* ------------------------------------------------------------------ */
/* Tastatur (Pendants zu handle_*_key in ui.rs)                        */

/* Rueckgabe true = Eingabemodus aktiv geblieben */
static bool
handle_text_input(cb_key_t key, char* buf, size_t buf_size, bool* mode) {
    switch (key.kind) {
        case CB_KEY_ESC:
            *mode = false;
            return false;
        case CB_KEY_BACKSPACE: {
            size_t len = strlen(buf);
            if (len > 0) {
                buf[len - 1] = '\0';
            }
            return true;
        }
        case CB_KEY_CHAR: {
            size_t len = strlen(buf);
            if (len + 1 < buf_size) {
                buf[len] = key.ch;
                buf[len + 1] = '\0';
            }
            return true;
        }
        default:
            return true;
    }
}

/* Rueckgabe true = Programm beenden */
static bool
handle_nodes_key(cb_ui_t* ui, cb_key_t key) {
    if (ui->node_filter_mode) {
        if (key.kind == CB_KEY_ENTER) {
            ui->node_filter_mode = false;
            apply_node_filter(ui);
            set_status(ui, "Filter angewendet: '%s'", ui->node_filter);
        } else {
            bool was_esc = key.kind == CB_KEY_ESC;
            handle_text_input(key, ui->node_filter, sizeof(ui->node_filter), &ui->node_filter_mode);
            apply_node_filter(ui);
            if (was_esc) {
                set_status(ui, "Filter verworfen");
            }
        }
        return false;
    }

    switch (key.kind) {
        case CB_KEY_CHAR:
            if (key.ch == 'q') {
                return true;
            }
            if (key.ch == '/') {
                ui->node_filter_mode = true;
                ui->node_filter[0] = '\0';
                set_status(ui, "Filter: tippen, Enter uebernehmen, Esc abbrechen");
            }
#ifdef CANBOSS_BERRY
            if (key.ch == 's') {
                run_berry_repl(ui);
            }
#endif
            break;
        case CB_KEY_UP:
            if (ui->selected_node > 0) {
                ui->selected_node--;
            }
            break;
        case CB_KEY_DOWN:
            if (ui->selected_node + 1 < ui->node_list_len) {
                ui->selected_node++;
            }
            break;
        case CB_KEY_ENTER:
            if (ui->selected_node < ui->node_list_len) {
                open_node(ui, ui->node_list[ui->selected_node]);
            }
            break;
        case CB_KEY_CTRL_C:
            return true;
        default:
            break;
    }
    return false;
}

static bool
handle_monitor_key(cb_ui_t* ui, cb_key_t key) {
    if (ui->filter_mode) {
        if (key.kind == CB_KEY_ENTER) {
            ui->filter_mode = false;
            apply_obj_filter(ui);
            set_status(ui, "Filter angewendet: '%s'", ui->filter);
        } else {
            handle_text_input(key, ui->filter, sizeof(ui->filter), &ui->filter_mode);
            apply_obj_filter(ui);
        }
        return false;
    }

    if (ui->write_mode) {
        if (key.kind == CB_KEY_ENTER) {
            write_selected(ui);
        } else {
            bool was_esc = key.kind == CB_KEY_ESC;
            handle_text_input(key, ui->write_buf, sizeof(ui->write_buf), &ui->write_mode);
            if (was_esc) {
                set_status(ui, "Schreiben abgebrochen");
            }
        }
        return false;
    }

    switch (key.kind) {
        case CB_KEY_CHAR:
            switch (key.ch) {
                case 'q':
                    return true;
                case 'b':
                    close_node(ui);
                    break;
                case '/':
                    ui->filter_mode = true;
                    ui->filter[0] = '\0';
                    set_status(ui, "Filter: tippen, Enter uebernehmen, Esc abbrechen");
                    break;
                case 'r':
                    read_selected(ui);
                    break;
                case 'R':
                    read_object(ui);
                    break;
                case 'w': {
                    const cb_dp_t* dp = current_dp(ui);
                    if (dp != NULL && dp->access != CB_ACC_RO) {
                        ui->write_mode = true;
                        ui->write_buf[0] = '\0';
                        set_status(ui, "Wert eingeben, Enter schreibt, Esc bricht ab");
                    } else {
                        set_status(ui, "Datenpunkt ist nicht schreibbar");
                    }
                    break;
                }
                case 'm':
                    ui->auto_refresh = !ui->auto_refresh;
                    set_status(ui, ui->auto_refresh ? "Auto-Refresh an" : "Auto-Refresh aus");
                    break;
#ifdef CANBOSS_BERRY
                case 's':
                    run_berry_repl(ui);
                    break;
#endif
                default:
                    break;
            }
            break;
        case CB_KEY_UP:
            if (ui->selected_obj > 0) {
                ui->selected_obj--;
                ui->selected_sub = 0;
            }
            break;
        case CB_KEY_DOWN:
            if (ui->selected_obj + 1 < ui->obj_list_len) {
                ui->selected_obj++;
                ui->selected_sub = 0;
            }
            break;
        case CB_KEY_LEFT:
            if (ui->selected_sub > 0) {
                ui->selected_sub--;
            }
            break;
        case CB_KEY_RIGHT:
        case CB_KEY_TAB: {
            const cb_obj_t* obj = current_obj(ui);
            if (obj != NULL && ui->selected_sub + 1 < obj->dp_count) {
                ui->selected_sub++;
            }
            break;
        }
        case CB_KEY_CTRL_C:
            return true;
        default:
            break;
    }
    return false;
}

/* ------------------------------------------------------------------ */

int
cb_ui_run(int start_node_id, const char* can_label) {
    cb_ui_t ui;
    memset(&ui, 0, sizeof(ui));
    ui.can_label = can_label;
    set_status(&ui, "Bereit");
    apply_node_filter(&ui);

    if (start_node_id > 0) {
        bool found = false;
        for (uint16_t i = 0; i < cb_net_node_count; i++) {
            if (cb_net_nodes[i]->node_id == (uint8_t)start_node_id) {
                if (open_node(&ui, i) != 0) {
                    return -1;
                }
                found = true;
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "Node %d ist nicht in eds/network.json definiert\n", start_node_id);
            return -1;
        }
    }

    if (tui_init() != 0) {
        fprintf(stderr, "Terminal-Init fehlgeschlagen (kein TTY?)\n");
        free(ui.values);
        free(ui.obj_list);
        return -1;
    }

    uint64_t last_refresh = now_ms();
    bool quit = false;

    while (!quit) {
        tui_frame_begin();
        if (ui.screen == SCREEN_NODES) {
            draw_nodes(&ui);
        } else {
            draw_monitor(&ui);
        }
        tui_frame_flush();

        if (ui.screen == SCREEN_MONITOR && ui.auto_refresh && cb_co_connected()
            && now_ms() - last_refresh >= CB_MONITOR_INTERVAL_MS) {
            read_object(&ui);
            last_refresh = now_ms();
        }

        cb_key_t key = tui_poll_key(100);
        if (key.kind == CB_KEY_NONE) {
            continue;
        }
        if (key.kind == CB_KEY_CTRL_C) {
            break;
        }
        if (ui.screen == SCREEN_NODES) {
            quit = handle_nodes_key(&ui, key);
        } else {
            quit = handle_monitor_key(&ui, key);
        }
    }

    tui_deinit();
    free(ui.values);
    free(ui.obj_list);
    return 0;
}
