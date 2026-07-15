/**
 * tui_io.h
 *
 * Plattform-I/O der Terminal-Schicht. Die Zeichen-/Layoutlogik in
 * tui.c ist plattformunabhaengig; nur diese vier Funktionen sind je
 * Ziel implementiert:
 *
 *   - tui_io_posix.c   Linux/POSIX: termios-Raw-Mode, select, TIOCGWINSZ
 *   - tui_io_zephyr.c  Zephyr: UART-Konsole (uart_poll_in/out),
 *                      Groesse aus CONFIG_CANBOSS_TUI_{ROWS,COLS}
 */

#ifndef CB_TUI_IO_H_
#define CB_TUI_IO_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Terminal in Raw-Mode versetzen (POSIX) bzw. UART pruefen (Zephyr).
 * Rueckgabe 0 bei Erfolg. */
int tui_io_init(void);
void tui_io_deinit(void);

/* Ein Byte lesen; wartet hoechstens timeout_ms. Rueckgabe 0..255 oder
 * -1, wenn nichts anliegt. */
int tui_io_getc(int timeout_ms);

/* Puffer komplett ausgeben. */
void tui_io_write(const char* buf, size_t len);

/* Terminalgroesse ermitteln; laesst rows/cols unveraendert, wenn die
 * Plattform sie nicht kennt. */
void tui_io_get_size(int* rows, int* cols);

#ifdef __cplusplus
}
#endif

#endif /* CB_TUI_IO_H_ */
