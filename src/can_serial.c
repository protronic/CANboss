/**
 * can_serial.c
 *
 * Platzhalter fuer die serielle CAN-Anbindung (geplant).
 *
 * Vorgesehen ist ein serieller CAN-Adapter am TTY (z.B. /dev/ttyACM0),
 * wie ihn das Rust-Original ueber das GTWA-JSON-Gateway angesprochen
 * hat (CANboss-rs/src/serial_gtwa.rs). Sobald das Frame-Format des
 * Adapters feststeht (slcan/GTWA-Binaerprotokoll), implementiert dieses
 * Backend open/send/recv ueber termios - die restliche Anwendung
 * (CANopenNode-Treiber, SDO, UI) bleibt unveraendert, da sie nur
 * cb_can_backend_t nutzt.
 */

#include "can_if.h"

#include <errno.h>
#include <stddef.h>

static int
serial_open(const char* device, uint32_t bitrate) {
    (void)device;
    (void)bitrate;
    errno = ENOSYS;
    return -1; /* noch nicht implementiert */
}

static void
serial_close(void) {
}

static int
serial_send(const cb_can_frame_t* frame) {
    (void)frame;
    errno = ENOSYS;
    return -1;
}

static int
serial_recv(cb_can_frame_t* frame, int timeout_ms) {
    (void)frame;
    (void)timeout_ms;
    errno = ENOSYS;
    return -1;
}

const cb_can_backend_t cb_can_serial = {
    .name = "serial",
    .open = serial_open,
    .close = serial_close,
    .send = serial_send,
    .recv = serial_recv,
};
