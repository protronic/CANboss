/**
 * Minimaler Berry-Port (kein Dateisystem, Ausgabe an umschaltbare Senke).
 */

#include "berry.h"
#include "berry_gpio.h"

#include <stdio.h>

static usbdemo_berry_sink_t cb_sink;
static void* cb_sink_user;

void
usbdemo_berry_set_sink(usbdemo_berry_sink_t sink, void* user) {
    cb_sink = sink;
    cb_sink_user = user;
}

BERRY_API void
be_writebuffer(const char* buffer, size_t length) {
    if (cb_sink != NULL) {
        cb_sink(cb_sink_user, buffer, length);
    } else {
        fwrite(buffer, 1, length, stdout);
    }
}

BERRY_API char*
be_readstring(char* buffer, size_t size) {
    (void)buffer;
    (void)size;
    return NULL;
}

void*
be_fopen(const char* filename, const char* modes) {
    (void)filename;
    (void)modes;
    return NULL;
}

int
be_fclose(void* hfile) {
    (void)hfile;
    return -1;
}

size_t
be_fwrite(void* hfile, const void* buffer, size_t length) {
    if (hfile == stdout || hfile == stderr) {
        be_writebuffer(buffer, length);
        return length;
    }
    return 0;
}

size_t
be_fread(void* hfile, void* buffer, size_t length) {
    (void)hfile;
    (void)buffer;
    (void)length;
    return 0;
}

char*
be_fgets(void* hfile, void* buffer, int size) {
    (void)hfile;
    (void)buffer;
    (void)size;
    return NULL;
}

int
be_fseek(void* hfile, long offset) {
    (void)hfile;
    (void)offset;
    return -1;
}

long int
be_ftell(void* hfile) {
    (void)hfile;
    return -1;
}

long int
be_fflush(void* hfile) {
    (void)hfile;
    return 0;
}

size_t
be_fsize(void* hfile) {
    (void)hfile;
    return 0;
}
