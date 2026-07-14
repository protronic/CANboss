/**
 * tui_io_posix.c
 *
 * Terminal-I/O fuer Linux/POSIX: termios-Raw-Mode, select auf stdin,
 * Fenstergroesse per TIOCGWINSZ. (Beim Zephyr-Build ersetzt durch
 * tui_io_zephyr.c.)
 */

#include "tui_io.h"

#include <stdbool.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

static struct termios tui_saved_termios;
static bool tui_raw_active = false;

int
tui_io_init(void) {
    struct termios raw;

    if (tcgetattr(STDIN_FILENO, &tui_saved_termios) != 0) {
        return -1;
    }
    raw = tui_saved_termios;
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(tcflag_t)(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(tcflag_t)OPOST;
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        return -1;
    }
    tui_raw_active = true;
    return 0;
}

void
tui_io_deinit(void) {
    if (tui_raw_active) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &tui_saved_termios);
        tui_raw_active = false;
    }
}

int
tui_io_getc(int timeout_ms) {
    fd_set rfds;
    struct timeval tv;

    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    if (select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv) <= 0) {
        return -1;
    }

    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1) {
        return -1;
    }
    return c;
}

void
tui_io_write(const char* buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(STDOUT_FILENO, buf + off, len - off);
        if (n <= 0) {
            return;
        }
        off += (size_t)n;
    }
}

void
tui_io_get_size(int* rows, int* cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    }
}
