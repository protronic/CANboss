/**
 * tui.c
 *
 * Terminal-Schicht: Frame-Puffer + ANSI-Escape-Ausgabe + Tastatur-
 * Parsing. Plattformspezifisches I/O (Raw-Mode, Byte lesen/schreiben,
 * Fenstergroesse) liegt hinter tui_io.h - implementiert fuer POSIX
 * (tui_io_posix.c) und Zephyr (tui_io_zephyr.c).
 *
 * Der Frame wird in einem Zellenpuffer aufgebaut und mit einem einzigen
 * write ausgegeben; Attributwechsel werden dabei minimiert.
 */

#include "tui.h"
#include "tui_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Puffergroesse: im Zephyr-Build auf die konfigurierte UI-Groesse
 * begrenzt (statische Puffer!), am Host grosszuegig fuer jedes
 * Terminalfenster. */
#ifdef CONFIG_CANBOSS_TUI_ROWS
#define TUI_MAX_ROWS CONFIG_CANBOSS_TUI_ROWS
#define TUI_MAX_COLS CONFIG_CANBOSS_TUI_COLS
#else
#define TUI_MAX_ROWS 200
#define TUI_MAX_COLS 400
#endif

typedef struct {
    char ch;
    uint8_t style;
} tui_cell_t;

static bool tui_active = false;
static int tui_cur_rows = 24;
static int tui_cur_cols = 80;
static tui_cell_t tui_buf[TUI_MAX_ROWS][TUI_MAX_COLS];

static void
tui_update_size(void) {
    int rows = tui_cur_rows;
    int cols = tui_cur_cols;
    tui_io_get_size(&rows, &cols);
    tui_cur_rows = rows > 0 ? (rows < TUI_MAX_ROWS ? rows : TUI_MAX_ROWS) : tui_cur_rows;
    tui_cur_cols = cols > 0 ? (cols < TUI_MAX_COLS ? cols : TUI_MAX_COLS) : tui_cur_cols;
}

int
tui_init(void) {
    if (tui_io_init() != 0) {
        return -1;
    }
    /* Alternate Screen an, Cursor aus */
    tui_io_write("\x1b[?1049h\x1b[?25l", 14);
    tui_active = true;
    tui_update_size();
    return 0;
}

void
tui_deinit(void) {
    if (!tui_active) {
        return;
    }
    tui_io_write("\x1b[?25h\x1b[?1049l", 14);
    tui_io_deinit();
    tui_active = false;
}

int
tui_rows(void) {
    return tui_cur_rows;
}

int
tui_cols(void) {
    return tui_cur_cols;
}

void
tui_frame_begin(void) {
    tui_update_size();
    for (int r = 0; r < tui_cur_rows; r++) {
        for (int c = 0; c < tui_cur_cols; c++) {
            tui_buf[r][c].ch = ' ';
            tui_buf[r][c].style = CB_ST_NONE;
        }
    }
}

static size_t
emit_style(char* out, uint8_t style) {
    size_t n = 0;
    n += (size_t)sprintf(out + n, "\x1b[0m");
    if (style & CB_ST_BOLD) {
        n += (size_t)sprintf(out + n, "\x1b[1m");
    }
    if (style & CB_ST_DIM) {
        n += (size_t)sprintf(out + n, "\x1b[2m");
    }
    if (style & CB_ST_REVERSE) {
        n += (size_t)sprintf(out + n, "\x1b[7m");
    }
    if (style & CB_ST_CYAN) {
        n += (size_t)sprintf(out + n, "\x1b[36m");
    }
    if (style & CB_ST_YELLOW) {
        n += (size_t)sprintf(out + n, "\x1b[33m");
    }
    return n;
}

void
tui_frame_flush(void) {
    /* Worst case: je Zelle ein Attributwechsel (~24 Byte) */
    static char out[TUI_MAX_ROWS * TUI_MAX_COLS * 26 + 64];
    size_t n = 0;
    uint8_t cur_style = 0xFF;

    n += (size_t)sprintf(out + n, "\x1b[H");
    for (int r = 0; r < tui_cur_rows; r++) {
        if (r > 0) {
            n += (size_t)sprintf(out + n, "\r\n");
        }
        for (int c = 0; c < tui_cur_cols; c++) {
            if (tui_buf[r][c].style != cur_style) {
                cur_style = tui_buf[r][c].style;
                n += emit_style(out + n, cur_style);
            }
            out[n++] = tui_buf[r][c].ch;
        }
    }
    n += (size_t)sprintf(out + n, "\x1b[0m");
    tui_io_write(out, n);
}

void
tui_put(int row, int col, uint8_t style, const char* text) {
    if (row < 0 || row >= tui_cur_rows || text == NULL) {
        return;
    }
    for (int c = col; *text != '\0'; text++, c++) {
        if (c < 0) {
            continue;
        }
        if (c >= tui_cur_cols) {
            break;
        }
        /* Nicht druckbare Zeichen (auch UTF-8-Folgebytes) ersetzen,
         * damit der Zellenpuffer spaltenstabil bleibt. */
        unsigned char ch = (unsigned char)*text;
        tui_buf[row][c].ch = (ch >= 0x20 && ch < 0x7F) ? (char)ch : '?';
        tui_buf[row][c].style = style;
    }
}

void
tui_box(int row, int col, int height, int width, uint8_t style, const char* title) {
    if (height < 2 || width < 2) {
        return;
    }

    for (int c = col; c < col + width && c < tui_cur_cols; c++) {
        if (row >= 0 && row < tui_cur_rows) {
            tui_buf[row][c].ch = '-';
            tui_buf[row][c].style = style;
        }
        int bottom = row + height - 1;
        if (bottom >= 0 && bottom < tui_cur_rows) {
            tui_buf[bottom][c].ch = '-';
            tui_buf[bottom][c].style = style;
        }
    }
    for (int r = row; r < row + height && r < tui_cur_rows; r++) {
        if (r < 0) {
            continue;
        }
        if (col >= 0 && col < tui_cur_cols) {
            tui_buf[r][col].ch = '|';
            tui_buf[r][col].style = style;
        }
        int right = col + width - 1;
        if (right >= 0 && right < tui_cur_cols) {
            tui_buf[r][right].ch = '|';
            tui_buf[r][right].style = style;
        }
    }
    if (row >= 0 && row < tui_cur_rows) {
        if (col >= 0 && col < tui_cur_cols) {
            tui_buf[row][col].ch = '+';
        }
        if (col + width - 1 < tui_cur_cols) {
            tui_buf[row][col + width - 1].ch = '+';
        }
    }
    if (row + height - 1 >= 0 && row + height - 1 < tui_cur_rows) {
        if (col >= 0 && col < tui_cur_cols) {
            tui_buf[row + height - 1][col].ch = '+';
        }
        if (col + width - 1 < tui_cur_cols) {
            tui_buf[row + height - 1][col + width - 1].ch = '+';
        }
    }

    if (title != NULL && title[0] != '\0') {
        char buf[256];
        snprintf(buf, sizeof(buf), " %s ", title);
        int maxlen = width - 4;
        if (maxlen > 0 && (int)strlen(buf) > maxlen) {
            buf[maxlen] = '\0';
        }
        tui_put(row, col + 2, style | CB_ST_CYAN | CB_ST_BOLD, buf);
    }
}

cb_key_t
tui_poll_key(int timeout_ms) {
    cb_key_t key = {CB_KEY_NONE, 0};

    int c = tui_io_getc(timeout_ms);
    if (c < 0) {
        return key;
    }

    switch (c) {
        case 0x03:
            key.kind = CB_KEY_CTRL_C;
            return key;
        case '\r':
        case '\n':
            key.kind = CB_KEY_ENTER;
            return key;
        case '\t':
            key.kind = CB_KEY_TAB;
            return key;
        case 0x7F:
        case 0x08:
            key.kind = CB_KEY_BACKSPACE;
            return key;
        case 0x1B:
            break; /* Escape-Sequenz unten */
        default:
            if (c >= 0x20 && c < 0x7F) {
                key.kind = CB_KEY_CHAR;
                key.ch = (char)c;
            }
            return key;
    }

    /* ESC: entweder allein oder Beginn einer CSI-Sequenz
     * (20 ms auf Folgebytes warten) */
    int b1 = tui_io_getc(20);
    if (b1 != '[') {
        key.kind = CB_KEY_ESC;
        return key;
    }
    int b2 = tui_io_getc(20);

    switch (b2) {
        case 'A': key.kind = CB_KEY_UP; break;
        case 'B': key.kind = CB_KEY_DOWN; break;
        case 'C': key.kind = CB_KEY_RIGHT; break;
        case 'D': key.kind = CB_KEY_LEFT; break;
        case 'Z': key.kind = CB_KEY_TAB; break; /* Shift-Tab wie Tab */
        default: key.kind = CB_KEY_ESC; break;
    }
    return key;
}
