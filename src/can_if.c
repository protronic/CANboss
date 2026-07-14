/**
 * can_if.c
 *
 * Registry der CAN-Backends. Neue Backends (z.B. weitere serielle
 * Protokolle) hier eintragen.
 */

#include "can_if.h"

#include <stddef.h>
#include <string.h>

extern const cb_can_backend_t cb_can_socketcan;
extern const cb_can_backend_t cb_can_serial;

static const cb_can_backend_t* const cb_backends[] = {
    &cb_can_socketcan,
    &cb_can_serial,
};

static const char* cb_backend_name_list[sizeof(cb_backends) / sizeof(cb_backends[0]) + 1];

const cb_can_backend_t*
cb_can_backend_find(const char* name) {
    if (name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(cb_backends) / sizeof(cb_backends[0]); i++) {
        if (strcmp(cb_backends[i]->name, name) == 0) {
            return cb_backends[i];
        }
    }
    return NULL;
}

const char* const*
cb_can_backend_names(void) {
    for (size_t i = 0; i < sizeof(cb_backends) / sizeof(cb_backends[0]); i++) {
        cb_backend_name_list[i] = cb_backends[i]->name;
    }
    cb_backend_name_list[sizeof(cb_backends) / sizeof(cb_backends[0])] = NULL;
    return cb_backend_name_list;
}
