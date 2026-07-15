/**
 * main.c
 *
 * CANboss - CANopen-Parametermonitor mit Terminal-UI (C-Port von
 * CANboss-rs). Statt CouchDB-Discovery und GTWA-Serial-Gateway nutzt
 * der Port die aus den EDS-Dateien generierten Tabellen (src/gen/)
 * und spricht CANopen direkt ueber CANopenNode - zunaechst ueber
 * SocketCAN, spaeter auch ueber einen seriellen CAN-Adapter
 * (--backend serial, siehe src/can_serial.c).
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "canboss.h"
#include "canboss_net.h"
#include "can_if.h"
#include "co_node.h"
#include "canboss_master.h" /* nach co_node.h (CANopenNode-Typen) */
#include "ui.h"

static void
usage(const char* prog) {
    printf("CANboss - CANopen-Parametermonitor (C-Port von CANboss-rs)\n"
           "\n"
           "Aufruf: %s [Optionen]\n"
           "\n"
           "  --backend <name>   CAN-Backend: socketcan (Default) | serial (geplant)\n"
           "  --can <iface>      CAN-Interface bzw. Geraet (Default: can0)\n"
           "  --bitrate <bps>    Bitrate fuer Backends, die sie selbst setzen\n"
           "                     (SocketCAN uebernimmt sie vom Interface)\n"
           "  --node <id>        Knoten aus eds/network.json direkt oeffnen\n"
           "  --node-id <id>     eigene Node-ID des Monitors (Default: %u)\n"
           "  --offline          ohne CAN-Verbindung starten (nur Browsen)\n"
           "  --list             bekannte Knoten ausgeben und beenden\n"
           "  --help             diese Hilfe\n"
           "\n"
           "Beispiele:\n"
           "  %s --can vcan0                 Netzwerk-Browser auf vcan0\n"
           "  %s --can can0 --node 16        Monitor fuer Node 16 (IO-Modul)\n"
           "  %s --offline                   Datenpunkte ohne Bus ansehen\n",
           prog, CB_CO_NODE_ID_DEFAULT, prog, prog, prog);
}

static void
list_nodes(void) {
    printf("Knoten aus eds/network.json (einkompiliert via tools/eds2tui.py):\n");
    for (uint16_t i = 0; i < cb_net_node_count; i++) {
        const cb_node_desc_t* n = cb_net_nodes[i];
        printf("  Node %3u  %-16s %-20s %u Objekte, %u Datenpunkte\n", n->node_id, n->name, n->eds_file, n->obj_count,
               n->dp_count);
    }
}

int
main(int argc, char** argv) {
    const char* backend_name = "socketcan";
    const char* device = "can0";
    uint32_t bitrate = 125000;
    int start_node = 0;
    int own_node_id = CB_CO_NODE_ID_DEFAULT;
    bool offline = false;

    static const struct option opts[] = {
        {"backend", required_argument, NULL, 'B'},
        {"can", required_argument, NULL, 'c'},
        {"bitrate", required_argument, NULL, 'b'},
        {"node", required_argument, NULL, 'n'},
        {"node-id", required_argument, NULL, 'i'},
        {"offline", no_argument, NULL, 'o'},
        {"list", no_argument, NULL, 'l'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "h", opts, NULL)) != -1) {
        switch (opt) {
            case 'B': backend_name = optarg; break;
            case 'c': device = optarg; break;
            case 'b': bitrate = (uint32_t)strtoul(optarg, NULL, 0); break;
            case 'n': start_node = atoi(optarg); break;
            case 'i': own_node_id = atoi(optarg); break;
            case 'o': offline = true; break;
            case 'l': list_nodes(); return 0;
            case 'h': usage(argv[0]); return 0;
            default: usage(argv[0]); return 1;
        }
    }

    if (own_node_id < 1 || own_node_id > 127) {
        fprintf(stderr, "Ungueltige eigene Node-ID %d (1..127)\n", own_node_id);
        return 1;
    }

    char can_label[128];
    snprintf(can_label, sizeof(can_label), "%s/%s", backend_name, device);

    /* OD-Konfiguration des Masters (muss den Stack ueberleben) */
    static CO_config_t cb_master_config;
    memset(&cb_master_config, 0, sizeof(cb_master_config));
    canboss_master_INIT_CONFIG(cb_master_config);

    if (!offline) {
        const cb_can_backend_t* backend = cb_can_backend_find(backend_name);
        if (backend == NULL) {
            fprintf(stderr, "Unbekanntes Backend '%s'. Verfuegbar:", backend_name);
            for (const char* const* n = cb_can_backend_names(); *n != NULL; n++) {
                fprintf(stderr, " %s", *n);
            }
            fprintf(stderr, "\n");
            return 1;
        }
        if (cb_co_start(canboss_master, &cb_master_config, backend, device, bitrate, (uint8_t)own_node_id) != 0) {
            /* wie das Rust-Original: Fehler melden und offline weiter */
            fprintf(stderr, "Warnung: %s - starte im Offline-Modus\n", cb_co_error());
        }
    }

    int ret = cb_ui_run(start_node, can_label);

    cb_co_stop();
    return ret == 0 ? 0 : 1;
}
