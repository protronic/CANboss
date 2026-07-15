/**
 * berry_repl.c
 *
 * Interaktive Berry-REPL auf einem Raw-Mode-Terminal (Monitor,
 * Taste 's'). Nutzt be_repl() aus dem Berry-Submodul - damit gibt es
 * Ausdrucks-Ausgabe ("1+2" -> 3) und Mehrzeilen-Fortsetzung ("def ..."
 * -> Prompt ">>") geschenkt. Hier drumherum: ein kleiner Zeileneditor
 * (Echo, Backspace, Strg-C verwirft die Zeile, Pfeil hoch/runter =
 * History), Banner und die \n -> \r\n Uebersetzung fuer den Raw-Mode.
 *
 * Ausstieg: "exit", "quit" oder Strg-D auf leerer Zeile.
 */

#include "co_node.h" /* OD-Typen fuer canboss_berry.h */

#include "canboss_berry.h"
#include "osal.h"

#include <stdio.h>
#include <string.h>

#include "berry.h"
#include "be_repl.h"

#define CB_REPL_LINE_MAX 256
#define CB_REPL_HISTORY  8

static const cb_berry_repl_io_t* cb_io;
static char cb_line[CB_REPL_LINE_MAX];
static char cb_history[CB_REPL_HISTORY][CB_REPL_LINE_MAX];
static int cb_hist_count;

/* ------------------------------------------------------------------ */
/* Ausgabe                                                             */

static void
repl_write(const char* s, size_t len) {
    cb_io->write_fn(s, len);
}

static void
repl_puts(const char* s) {
    repl_write(s, strlen(s));
}

/* Skript-Ausgabe (print, Fehlermeldungen): '\n' -> "\r\n" */
static void
repl_sink(void* user, const char* buf, size_t len) {
    (void)user;
    size_t start = 0;
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == '\n') {
            if (i > start) {
                repl_write(buf + start, i - start);
            }
            repl_puts("\r\n");
            start = i + 1;
        }
    }
    if (len > start) {
        repl_write(buf + start, len - start);
    }
}

static void
repl_prompt(const char* prompt) {
    repl_puts("\x1b[1;36mberry\x1b[0m");
    repl_puts(prompt);
}

/* ------------------------------------------------------------------ */
/* Eingabe                                                             */

static int
repl_getc_blocking(void) {
    for (;;) {
        int c = cb_io->getc_fn(50);
        if (c >= 0) {
            return c;
        }
    }
}

static void
history_add(const char* line) {
    if (line[0] == '\0') {
        return;
    }
    if (cb_hist_count > 0 && strcmp(cb_history[0], line) == 0) {
        return; /* Doppler direkt hintereinander nicht speichern */
    }
    if (cb_hist_count < CB_REPL_HISTORY) {
        cb_hist_count++;
    }
    memmove(cb_history[1], cb_history[0], (size_t)(cb_hist_count - 1) * CB_REPL_LINE_MAX);
    snprintf(cb_history[0], CB_REPL_LINE_MAX, "%s", line);
}

/* Zeile loeschen und mit neuem Inhalt neu zeichnen */
static void
redraw_line(const char* prompt, size_t len) {
    repl_puts("\r\x1b[K");
    repl_prompt(prompt);
    repl_write(cb_line, len);
}

/* readline-Callback fuer be_repl(); prompt ist "> " oder ">> " */
static char*
repl_getline(const char* prompt) {
    size_t len = 0;
    int hist_pos = -1; /* -1 = frische Eingabe */

    repl_prompt(prompt);

    for (;;) {
        int c = repl_getc_blocking();

        if (c == '\r' || c == '\n') {
            repl_puts("\r\n");
            cb_line[len] = '\0';
            if (strcmp(cb_line, "exit") == 0 || strcmp(cb_line, "quit") == 0) {
                return NULL;
            }
            history_add(cb_line);
            return cb_line;
        } else if (c == 0x7F || c == 0x08) { /* Backspace */
            if (len > 0) {
                len--;
                repl_puts("\b \b");
            }
        } else if (c == 0x03) { /* Strg-C: Zeile verwerfen */
            repl_puts("^C\r\n");
            len = 0;
            hist_pos = -1;
            repl_prompt(prompt);
        } else if (c == 0x04) { /* Strg-D auf leerer Zeile: Ende */
            if (len == 0) {
                repl_puts("\r\n");
                return NULL;
            }
        } else if (c == 0x1B) { /* Escape-Sequenz (Pfeile etc.) */
            int c1 = cb_io->getc_fn(20);
            if (c1 == '[' || c1 == 'O') {
                int c2 = repl_getc_blocking();
                if (c2 == 'A' && hist_pos + 1 < cb_hist_count) { /* hoch */
                    hist_pos++;
                    len = strlen(cb_history[hist_pos]);
                    memcpy(cb_line, cb_history[hist_pos], len);
                    redraw_line(prompt, len);
                } else if (c2 == 'B') { /* runter */
                    if (hist_pos > 0) {
                        hist_pos--;
                        len = strlen(cb_history[hist_pos]);
                        memcpy(cb_line, cb_history[hist_pos], len);
                    } else {
                        hist_pos = -1;
                        len = 0;
                    }
                    redraw_line(prompt, len);
                }
                /* uebrige Parameterzeichen der Sequenz schlucken */
                while (c2 >= '0' && c2 <= '9') {
                    c2 = repl_getc_blocking();
                }
            }
        } else if (c >= 0x20 && c != 0x7F) { /* druckbar (inkl. UTF-8) */
            if (len + 1 < sizeof(cb_line)) {
                char ch = (char)c;
                cb_line[len++] = ch;
                repl_write(&ch, 1);
            }
        }
    }
}

/* ------------------------------------------------------------------ */

void
canboss_berry_repl(const cb_berry_repl_io_t* io) {
    bvm* vm = (bvm*)canboss_berry_vm();
    if (vm == NULL || io == NULL) {
        return;
    }
    cb_io = io;

    cb_berry_set_sink(repl_sink, NULL);
    repl_puts("\x1b[0m\x1b[2J\x1b[H");
    repl_puts("\x1b[1;36mBerry " BERRY_VERSION "\x1b[0m - Skript-REPL des CANboss-Monitors\r\n");
    repl_puts("  \x1b[1mhelp()\x1b[0m Beispiele und od_*-API   "
              "\x1b[1mexit\x1b[0m/Strg-D zurueck zur Oberflaeche\r\n\r\n");

    cb_mutex_lock(CB_MUTEX_BERRY);
    (void)be_repl(vm, repl_getline, NULL);
    cb_mutex_unlock(CB_MUTEX_BERRY);

    cb_berry_set_sink(NULL, NULL);
    cb_io = NULL;
}
