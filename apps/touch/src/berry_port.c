/**
 * berry_port.c
 *
 * Berry-Portierungsschicht der Zephyr-App (ersetzt berry/default/be_port.c):
 *
 *  - be_writebuffer(): Skript-Ausgabe (print, Fehlermeldungen) geht an
 *    eine umschaltbare Senke - waehrend eines Shell-Kommandos an die
 *    Shell, sonst an stdout/Konsole.
 *  - kein Dateisystem: Datei-API als Stubs (BE_USE_FILE_SYSTEM 0).
 */

#include "berry.h"

#include <stdio.h>
#include <string.h>

#include "canboss_berry.h"

static cb_berry_sink_t cb_sink;
static void* cb_sink_user;

void
cb_berry_set_sink(cb_berry_sink_t sink, void* user) {
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
    return NULL; /* kein interaktives stdin (REPL laeuft ueber die Shell) */
}

/* Datei-API: ohne Dateisystem nur Stubs (BE_USE_FILE_SYSTEM 0) */

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
