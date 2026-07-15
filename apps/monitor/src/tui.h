/**
 * tui.h
 *
 * Minimale Terminal-Schicht (Pendant zu crossterm im Rust-Original):
 * Raw-Mode, Alternate Screen, Tastatur mit Escape-Sequenzen und ein
 * zeichenbasierter Frame-Puffer, der pro Bild als Ganzes ausgegeben
 * wird (flimmerfrei, keine ncurses-Abhaengigkeit).
 */

#ifndef CB_TUI_H_
#define CB_TUI_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CB_KEY_NONE = 0,
    CB_KEY_CHAR, /* druckbares Zeichen in .ch */
    CB_KEY_UP,
    CB_KEY_DOWN,
    CB_KEY_LEFT,
    CB_KEY_RIGHT,
    CB_KEY_ENTER,
    CB_KEY_ESC,
    CB_KEY_TAB,
    CB_KEY_BACKSPACE,
    CB_KEY_CTRL_C,
} cb_key_kind_t;

typedef struct {
    cb_key_kind_t kind;
    char ch;
} cb_key_t;

/* Textattribute je Zelle */
enum {
    CB_ST_NONE = 0,
    CB_ST_BOLD = 1 << 0,
    CB_ST_REVERSE = 1 << 1, /* Auswahlbalken */
    CB_ST_DIM = 1 << 2,     /* deaktiviert/Beschreibung */
    CB_ST_CYAN = 1 << 3,    /* Tabellenkopf/Rahmen-Titel */
    CB_ST_YELLOW = 1 << 4,  /* Subobjekt-Auswahl */
};

int tui_init(void);
void tui_deinit(void);

/* Terminalgroesse (bei jedem Frame neu ermittelt) */
int tui_rows(void);
int tui_cols(void);

/* Frame aufbauen: begin loescht den Puffer, flush gibt ihn aus. */
void tui_frame_begin(void);
void tui_frame_flush(void);

/* Text an (row, col) mit Attributen schreiben (clippt am Rand). */
void tui_put(int row, int col, uint8_t style, const char* text);

/* ASCII-Rahmen mit Titel zeichnen */
void tui_box(int row, int col, int height, int width, uint8_t style, const char* title);

/* Naechste Taste lesen; wartet hoechstens timeout_ms (0 = nicht warten). */
cb_key_t tui_poll_key(int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* CB_TUI_H_ */
