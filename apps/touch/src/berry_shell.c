/**
 * berry_shell.c
 *
 * Zephyr-Shell-Kommando "berry" des Touch-Panels: fuehrt Skripttext
 * ueber die gemeinsame Berry-Schicht (lib/berry_od) aus, Ausgabe geht
 * an die Shell. `berry help()` zeigt Beispiele und die od_*-API.
 */

#include <stdio.h>

#include <zephyr/shell/shell.h>

#include "co_node.h" /* OD-Typen fuer canboss_berry.h */

#include "canboss_berry.h"

static void
cb_shell_sink(void* user, const char* buf, size_t len) {
    const struct shell* sh = (const struct shell*)user;
    shell_fprintf(sh, SHELL_NORMAL, "%.*s", (int)len, buf);
}

static int
cmd_berry(const struct shell* sh, size_t argc, char** argv) {
    /* Argumente wieder zu einem Skripttext zusammensetzen */
    static char code[CONFIG_SHELL_CMD_BUFF_SIZE];
    code[0] = '\0';
    size_t pos = 0;
    for (size_t i = 1; i < argc; i++) {
        int w = snprintf(code + pos, sizeof(code) - pos, "%s%s", i > 1 ? " " : "", argv[i]);
        if (w < 0 || (size_t)w >= sizeof(code) - pos) {
            break;
        }
        pos += (size_t)w;
    }

    cb_berry_set_sink(cb_shell_sink, (void*)sh);
    int ret = canboss_berry_exec(code);
    cb_berry_set_sink(NULL, NULL);
    return ret;
}

SHELL_CMD_ARG_REGISTER(berry, NULL,
                       "Berry-Skript ausfuehren; 'berry help()' zeigt Beispiele, "
                       "z.B.: berry print(od_read(127,0x1017,0))",
                       cmd_berry, 2, 32);
