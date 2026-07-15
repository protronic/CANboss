/**
 * tui_io_zephyr.c
 *
 * Terminal-I/O fuer Zephyr: die UART-Konsole (chosen zephyr,console)
 * im Poll-Betrieb. Auf native_sim liegt sie mit
 * CONFIG_NATIVE_UART_0_ON_STDINOUT direkt auf stdin/stdout des
 * Host-Prozesses; auf Hardware ist die UART ohnehin "raw".
 *
 * Die Terminalgroesse ist nicht abfragbar und kommt aus
 * CONFIG_CANBOSS_TUI_{ROWS,COLS}.
 */

#ifdef CONFIG_NATIVE_LIBC
/* native_sim mit Host-libc: Feature-Makros fuer <termios.h>, muessen
 * vor dem ersten libc-Include stehen. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#endif

#include "tui_io.h"

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

#ifdef CONFIG_NATIVE_LIBC
#include <stdbool.h>
#include <termios.h>
#include <unistd.h>
#endif

static const struct device* const tui_uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

#ifdef CONFIG_NATIVE_LIBC
/* Der native UART-Treiber setzt nur sein eigenes PTY in den Raw-Mode
 * (NATIVE_UART_0_ON_OWN_PTY); liegt die Konsole per
 * NATIVE_UART_0_ON_STDINOUT auf stdin/stdout, buffert das
 * Host-Terminal sonst zeilenweise und echot die Eingabe. */
static struct termios tui_saved_termios;
static bool tui_raw_active;

static void
tui_host_raw_mode(void) {
    struct termios raw;

    if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &tui_saved_termios) != 0) {
        return;
    }
    raw = tui_saved_termios;
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(tcflag_t)(IXON | ICRNL);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
        tui_raw_active = true;
    }
}

static void
tui_host_restore_mode(void) {
    if (tui_raw_active) {
        tcsetattr(STDIN_FILENO, TCSANOW, &tui_saved_termios);
        tui_raw_active = false;
    }
}
#endif /* CONFIG_NATIVE_LIBC */

int
tui_io_init(void) {
    if (!device_is_ready(tui_uart)) {
        return -1;
    }
#ifdef CONFIG_NATIVE_LIBC
    tui_host_raw_mode();
#endif
    return 0;
}

void
tui_io_deinit(void) {
#ifdef CONFIG_NATIVE_LIBC
    tui_host_restore_mode();
#endif
}

int
tui_io_getc(int timeout_ms) {
    int64_t deadline = k_uptime_get() + timeout_ms;
    unsigned char c;

    for (;;) {
        if (uart_poll_in(tui_uart, &c) == 0) {
            return c;
        }
        if (k_uptime_get() >= deadline) {
            return -1;
        }
        k_sleep(K_MSEC(1));
    }
}

void
tui_io_write(const char* buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uart_poll_out(tui_uart, (unsigned char)buf[i]);
    }
}

void
tui_io_get_size(int* rows, int* cols) {
    *rows = CONFIG_CANBOSS_TUI_ROWS;
    *cols = CONFIG_CANBOSS_TUI_COLS;
}
